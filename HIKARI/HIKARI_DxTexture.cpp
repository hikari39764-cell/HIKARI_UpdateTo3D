#include "HIKARI_DxTexture.h"
#include <base/DirectXCommon.h>
#include <cassert>
#include <cstdio>
#include "../External/WICTextureLoader.h"

using Microsoft::WRL::ComPtr;

namespace HIKARI {
    namespace DXTEX {

        // ===== 静态成员 =====
        bool  DxTextureManager::initialized_ = false;
        UINT  DxTextureManager::descriptorSize_ = 0;

        ComPtr<ID3D12DescriptorHeap> DxTextureManager::srvHeap_;

        std::vector<ComPtr<ID3D12Resource>>       DxTextureManager::textures_;
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>  DxTextureManager::srvCpu_;
        std::vector<D3D12_GPU_DESCRIPTOR_HANDLE>  DxTextureManager::srvGpu_;
        std::unordered_map<std::string, int>      DxTextureManager::nameToHandle_;

        int DxTextureManager::nextIndex_ = 0;



        // 简单的存在检查
        static bool FileExists(const std::string& path) {
            FILE* fp = nullptr;
            fopen_s(&fp, path.c_str(), "rb");
            if (fp) { fclose(fp); return true; }
            return false;
        }

        // ------------------------------------------------------
        void DxTextureManager::Init(int maxTextures)
        {
            if (initialized_) return;

            auto* device = KamataEngine::DirectXCommon::GetInstance()->GetDevice();

            // SRV Heap
            D3D12_DESCRIPTOR_HEAP_DESC desc{};
            desc.NumDescriptors = maxTextures;
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

            HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvHeap_));
            assert(SUCCEEDED(hr));

            descriptorSize_ =
                device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

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

        void DxTextureManager::Finalize()
        {
            textures_.clear();
            srvCpu_.clear();
            srvGpu_.clear();
            srvHeap_.Reset();
            nameToHandle_.clear();
            nextIndex_ = 0;
            DXTEX::CleanupWICResources();
            initialized_ = false;
        }

        void DxTextureManager::EnsureInit()
        {
            if (!initialized_) {
                Init(); // 用默认 128
            }
        }

        // ------------------------------------------------------
        int DxTextureManager::LoadTexture(const std::string& name, const std::string& path)
        {
            EnsureInit();

            // 已经加载过
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

        // ------------------------------------------------------
        int DxTextureManager::CreateTextureFromFile(const std::string& path)
        {
            auto* dx = KamataEngine::DirectXCommon::GetInstance();
            auto* device = dx->GetDevice();
            auto* cmdList = dx->GetCommandList();
            wchar_t wpath[260]{};
            mbstowcs_s(nullptr, wpath, path.c_str(), _TRUNCATE);

            Microsoft::WRL::ComPtr<ID3D12Resource> texResource;

            HRESULT hr = HIKARI::DXTEX::CreateWICTextureFromFile(
                device, cmdList,wpath, texResource.GetAddressOf());
            if (FAILED(hr) || !texResource) {
                OutputDebugStringA("DxTextureManager::CreateTextureFromFile - WIC load failed.\n");
                return -1;
            }

            int handle = nextIndex_++;
            if (handle >= static_cast<int>(textures_.size())) {
                textures_.resize(handle + 1);
                srvCpu_.resize(handle + 1);
                srvGpu_.resize(handle + 1);
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
            EnsureInit();
            if (!resource) {
                return -1;
            }

            auto* device = KamataEngine::DirectXCommon::GetInstance()->GetDevice();


            int handle = nextIndex_++;

            if (handle >= static_cast<int>(textures_.size())) {

                OutputDebugStringA("DxTextureManager::RegisterFromResource - out of texture slots.\n");
                return -1;
            }

            textures_[handle] = resource;


            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            auto desc = resource->GetDesc();
            srvDesc.Format = desc.Format;
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



        // ------------------------------------------------------
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
