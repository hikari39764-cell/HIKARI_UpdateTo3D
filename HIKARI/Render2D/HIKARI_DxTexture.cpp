#include "HIKARI_DxTexture.h"
#include <cassert>
#include <cstdio>
#include "../External/WICTextureLoader.h"

using Microsoft::WRL::ComPtr;

namespace HIKARI {
    namespace DXTEX {

        bool  DxTextureManager::initialized_ = false;
        GFX::Context DxTextureManager::context_{};
        UINT  DxTextureManager::descriptorSize_ = 0;

        ComPtr<ID3D12DescriptorHeap> DxTextureManager::srvHeap_;
        ComPtr<ID3D12CommandAllocator> DxTextureManager::uploadAllocator_;
        ComPtr<ID3D12GraphicsCommandList> DxTextureManager::uploadCmdList_;
        ComPtr<ID3D12Fence> DxTextureManager::uploadFence_;
        HANDLE DxTextureManager::uploadFenceEvent_ = nullptr;
        uint64_t DxTextureManager::uploadFenceValue_ = 1;

        std::vector<ComPtr<ID3D12Resource>>       DxTextureManager::textures_;
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>  DxTextureManager::srvCpu_;
        std::vector<D3D12_GPU_DESCRIPTOR_HANDLE>  DxTextureManager::srvGpu_;
        std::unordered_map<std::string, int>      DxTextureManager::nameToHandle_;

        int DxTextureManager::nextIndex_ = 0;

        void DxTextureManager::Init(const GFX::Context& ctx, int maxTextures)
        {
            if (initialized_) return;
            context_ = ctx;
            auto* device = context_.device;
            assert(device);

            if (context_.srvHeap) {
                srvHeap_ = context_.srvHeap;
            } else {
                D3D12_DESCRIPTOR_HEAP_DESC desc{};
                desc.NumDescriptors = maxTextures;
                desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvHeap_));
                assert(SUCCEEDED(hr));
            }

            descriptorSize_ =
                device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

            HRESULT hr = device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&uploadAllocator_));
            assert(SUCCEEDED(hr));

            hr = device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                uploadAllocator_.Get(),
                nullptr,
                IID_PPV_ARGS(&uploadCmdList_));
            assert(SUCCEEDED(hr));
            uploadCmdList_->Close();

            hr = device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&uploadFence_));
            assert(SUCCEEDED(hr));

            uploadFenceEvent_ = CreateEvent(nullptr, FALSE, FALSE, nullptr);
            assert(uploadFenceEvent_ != nullptr);
            uploadFenceValue_ = 1;

            srvCpu_.resize(maxTextures);
            srvGpu_.resize(maxTextures);
            textures_.resize(maxTextures);

            D3D12_CPU_DESCRIPTOR_HANDLE cpuStart = srvHeap_->GetCPUDescriptorHandleForHeapStart();
            D3D12_GPU_DESCRIPTOR_HANDLE gpuStart = srvHeap_->GetGPUDescriptorHandleForHeapStart();

            for (int i = 0; i < maxTextures; ++i) {
                srvCpu_[i].ptr = cpuStart.ptr + UINT64(i) * descriptorSize_;
                srvGpu_[i].ptr = gpuStart.ptr + UINT64(i) * descriptorSize_;
            }

            nextIndex_ = 0;
            initialized_ = true;
        }

        void DxTextureManager::UpdateContext(const GFX::Context& ctx) {
            context_ = ctx;
        }

        void DxTextureManager::Finalize()
        {
            textures_.clear();
            srvCpu_.clear();
            srvGpu_.clear();
            if (!context_.srvHeap) {
                srvHeap_.Reset();
            }
            uploadCmdList_.Reset();
            uploadAllocator_.Reset();
            uploadFence_.Reset();
            if (uploadFenceEvent_) {
                CloseHandle(uploadFenceEvent_);
                uploadFenceEvent_ = nullptr;
            }
            uploadFenceValue_ = 1;
            nameToHandle_.clear();
            nextIndex_ = 0;
            DXTEX::CleanupWICResources();
            initialized_ = false;
        }

        void DxTextureManager::EnsureInit()
        {
            if (!initialized_) {
                Init(context_);
            }
        }

        int DxTextureManager::LoadTexture(const std::string& name, const std::string& path)
        {
            EnsureInit();
            auto it = nameToHandle_.find(name);
            if (it != nameToHandle_.end()) {
                return it->second;
            }

            int handle = CreateTextureFromFile(path);
            if (handle >= 0) {
                nameToHandle_[name] = handle;
            }
            return handle;
        }

        int DxTextureManager::CreateTextureFromFile(const std::string& path)
        {
            auto* device = context_.device;
            auto* queue = context_.queue;
            assert(device && queue);

            assert(uploadAllocator_ && uploadCmdList_ && uploadFence_ && uploadFenceEvent_);

            HRESULT hr = uploadAllocator_->Reset();
            assert(SUCCEEDED(hr));

            hr = uploadCmdList_->Reset(uploadAllocator_.Get(), nullptr);
            assert(SUCCEEDED(hr));

            wchar_t wpath[260]{};
            mbstowcs_s(nullptr, wpath, path.c_str(), _TRUNCATE);

            Microsoft::WRL::ComPtr<ID3D12Resource> texResource;

            hr = HIKARI::DXTEX::CreateWICTextureFromFile(
                device, uploadCmdList_.Get(), wpath, texResource.GetAddressOf());
            if (FAILED(hr) || !texResource) {
                uploadCmdList_->Close();
                OutputDebugStringA("DxTextureManager::CreateTextureFromFile - WIC load failed.\n");
                return -1;
            }

            hr = uploadCmdList_->Close();
            assert(SUCCEEDED(hr));

            ID3D12CommandList* lists[] = { uploadCmdList_.Get() };
            queue->ExecuteCommandLists(1, lists);

            const uint64_t signalValue = uploadFenceValue_++;
            hr = queue->Signal(uploadFence_.Get(), signalValue);
            assert(SUCCEEDED(hr));

            if (uploadFence_->GetCompletedValue() < signalValue) {
                hr = uploadFence_->SetEventOnCompletion(signalValue, uploadFenceEvent_);
                assert(SUCCEEDED(hr));
                WaitForSingleObject(uploadFenceEvent_, INFINITE);
            }

            if (FAILED(hr) || !texResource) {
                OutputDebugStringA("DxTextureManager::CreateTextureFromFile - WIC load failed.\n");
                return -1;
            }

            int handle = nextIndex_++;
            if (handle >= static_cast<int>(textures_.size())) {
                return -1;
            }

            textures_[handle] = texResource;

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = texResource->GetDesc().Format;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;

            device->CreateShaderResourceView(
                texResource.Get(), &srvDesc, srvCpu_[handle]);

            return handle;
        }


        int DxTextureManager::RegisterFromResource(ID3D12Resource* resource)
        {
            if (!resource) {
                return -1;
            }
            return RegisterFromResourceAs(resource, resource->GetDesc().Format);
        }

        int DxTextureManager::RegisterFromResourceAs(ID3D12Resource* resource, DXGI_FORMAT srvFormat)
        {
            EnsureInit();
            if (!resource) {
                return -1;
            }

            auto* device = context_.device;

            int handle = nextIndex_++;

            if (handle >= static_cast<int>(textures_.size())) {
                OutputDebugStringA("DxTextureManager::RegisterFromResource - out of texture slots.\n");
                return -1;
            }

            textures_[handle] = resource;

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            auto desc = resource->GetDesc();
            srvDesc.Format = srvFormat;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;

            device->CreateShaderResourceView(
                resource,
                &srvDesc,
                srvCpu_[handle]
            );

            return handle;
        }

        D3D12_GPU_DESCRIPTOR_HANDLE DxTextureManager::GetSrvGpuHandle(int handle)
        {
            if (handle < 0 || handle >= static_cast<int>(srvGpu_.size())) {
                D3D12_GPU_DESCRIPTOR_HANDLE nullHandle{};
                nullHandle.ptr = 0;
                return nullHandle;
            }
            return srvGpu_[handle];
        }

        ID3D12DescriptorHeap* DxTextureManager::GetSrvHeap()
        {
            return srvHeap_.Get();
        }

        void DxTextureManager::GetTextureSize(int handle, UINT& outWidth, UINT& outHeight) {
            outWidth = 0;
            outHeight = 0;
            if (!initialized_) { return; }
            if (handle < 0 || handle >= static_cast<int>(textures_.size())) { return; }
            if (!textures_[handle]) { return; }

            auto desc = textures_[handle]->GetDesc();
            outWidth = static_cast<UINT>(desc.Width);
            outHeight = static_cast<UINT>(desc.Height);
        }


    } // namespace DXTEX
} // namespace HIKARI
