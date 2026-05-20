#include "HIKARI_DxTexture.h"
#include <algorithm>
#include <cassert>
#include <cctype>
#include <cstdio>
#include <sstream>
#include <DirectXTex.h>
#include <d3dx12.h>
#include "../External/WICTextureLoader.h"
#include "Core/HIKARI_Logger.h"

using Microsoft::WRL::ComPtr;

namespace HIKARI {
    namespace DXTEX {

        namespace {
            std::string ToLowerCopy(std::string value)
            {
                std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                    return static_cast<char>(std::tolower(c));
                });
                return value;
            }

            const char* ColorSpaceSuffix(TextureColorSpace colorSpace)
            {
                switch (colorSpace) {
                case TextureColorSpace::Linear:
                    return "|linear";
                case TextureColorSpace::Srgb:
                    return "|srgb";
                case TextureColorSpace::Auto:
                default:
                    return "|auto";
                }
            }

            std::string MakeTextureCacheKey(const std::string& name, TextureColorSpace colorSpace)
            {
                return name + ColorSpaceSuffix(colorSpace);
            }

            bool ContainsAny(const std::string& text, std::initializer_list<const char*> needles)
            {
                for (const char* needle : needles) {
                    if (needle != nullptr && text.find(needle) != std::string::npos) {
                        return true;
                    }
                }
                return false;
            }

            TextureColorSpace ResolveAutoColorSpace(const std::string& name, const std::string& path)
            {
                const std::string key = ToLowerCopy(name + " " + path);

                // Data textures must stay linear. Sampling these through an SRGB SRV would corrupt values.
                if (ContainsAny(key, {
                    "normal", "_n.", "_n_", "nrm",
                    "roughness", "metallic", "metalness", "metal_rough", "metallicroughness",
                    "occlusion", "ambientocclusion", "ao.", "_ao", "orm", "arm",
                    "height", "displacement", "mask", "opacity", "alpha", "linear"
                })) {
                    return TextureColorSpace::Linear;
                }

                // Most artist-authored color textures, UI textures, albedo/baseColor and emissive maps are SRGB.
                return TextureColorSpace::Srgb;
            }

            DXGI_FORMAT ToSrgbFormat(DXGI_FORMAT format)
            {
                switch (format) {
                case DXGI_FORMAT_R8G8B8A8_UNORM:
                    return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
                case DXGI_FORMAT_B8G8R8A8_UNORM:
                    return DXGI_FORMAT_B8G8R8A8_UNORM_SRGB;
                case DXGI_FORMAT_B8G8R8X8_UNORM:
                    return DXGI_FORMAT_B8G8R8X8_UNORM_SRGB;
                case DXGI_FORMAT_BC1_UNORM:
                    return DXGI_FORMAT_BC1_UNORM_SRGB;
                case DXGI_FORMAT_BC2_UNORM:
                    return DXGI_FORMAT_BC2_UNORM_SRGB;
                case DXGI_FORMAT_BC3_UNORM:
                    return DXGI_FORMAT_BC3_UNORM_SRGB;
                case DXGI_FORMAT_BC7_UNORM:
                    return DXGI_FORMAT_BC7_UNORM_SRGB;
                default:
                    return format;
                }
            }

            DXGI_FORMAT ToLinearFormat(DXGI_FORMAT format)
            {
                switch (format) {
                case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
                    return DXGI_FORMAT_R8G8B8A8_UNORM;
                case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
                    return DXGI_FORMAT_B8G8R8A8_UNORM;
                case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
                    return DXGI_FORMAT_B8G8R8X8_UNORM;
                case DXGI_FORMAT_BC1_UNORM_SRGB:
                    return DXGI_FORMAT_BC1_UNORM;
                case DXGI_FORMAT_BC2_UNORM_SRGB:
                    return DXGI_FORMAT_BC2_UNORM;
                case DXGI_FORMAT_BC3_UNORM_SRGB:
                    return DXGI_FORMAT_BC3_UNORM;
                case DXGI_FORMAT_BC7_UNORM_SRGB:
                    return DXGI_FORMAT_BC7_UNORM;
                default:
                    return format;
                }
            }

            DXGI_FORMAT ResolveSrvFormat(DXGI_FORMAT resourceFormat, TextureColorSpace colorSpace)
            {
                if (colorSpace == TextureColorSpace::Srgb) {
                    return ToSrgbFormat(resourceFormat);
                }
                if (colorSpace == TextureColorSpace::Linear) {
                    return ToLinearFormat(resourceFormat);
                }
                return resourceFormat;
            }
        }

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
        std::vector<TextureDimension>             DxTextureManager::dimensions_;
        std::vector<D3D12_CPU_DESCRIPTOR_HANDLE>  DxTextureManager::srvCpu_;
        std::vector<D3D12_GPU_DESCRIPTOR_HANDLE>  DxTextureManager::srvGpu_;
        std::unordered_map<std::string, int>      DxTextureManager::nameToHandle_;
        GFX::DescriptorAllocator                  DxTextureManager::descriptorAllocator_;

        void DxTextureManager::Init(const GFX::Context& ctx, int maxTextures)
        {
            if (initialized_) return;
            context_ = ctx;
            auto* device = context_.device;
            assert(device);

            maxTextures = std::clamp(
                maxTextures,
                1,
                static_cast<int>(GFX::DESCRIPTOR::kUserSrvCount));

            if (context_.srvHeap) {
                srvHeap_ = context_.srvHeap;
            } else {
                D3D12_DESCRIPTOR_HEAP_DESC desc{};
                desc.NumDescriptors = maxTextures;
                desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
                desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
                HRESULT hr = device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&srvHeap_));
                if (FAILED(hr)) {
                    OutputDebugStringA("DxTextureManager::Init - CreateDescriptorHeap failed.\n");
                    return;
                }
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
            dimensions_.resize(maxTextures, TextureDimension::Texture2D);
            descriptorAllocator_.Initialize(
                GFX::DESCRIPTOR::kUserSrvBegin,
                static_cast<UINT>(maxTextures));

            for (int i = 0; i < maxTextures; ++i) {
                const UINT descriptorIndex =
                    GFX::DESCRIPTOR::kUserSrvBegin + static_cast<UINT>(i);
                srvCpu_[i] =
                    GFX::DESCRIPTOR::CpuAt(srvHeap_.Get(), descriptorSize_, descriptorIndex);
                srvGpu_[i] =
                    GFX::DESCRIPTOR::GpuAt(srvHeap_.Get(), descriptorSize_, descriptorIndex);
            }

            initialized_ = true;
        }

        void DxTextureManager::UpdateContext(const GFX::Context& ctx) {
            context_ = ctx;
        }

        void DxTextureManager::Finalize()
        {
            textures_.clear();
            dimensions_.clear();
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
            descriptorAllocator_.Reset();
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
            return LoadTextureWithColorSpace(name, path, TextureColorSpace::Auto);
        }

        int DxTextureManager::LoadTextureWithColorSpace(const std::string& name, const std::string& path, TextureColorSpace colorSpace)
        {
            EnsureInit();
            const TextureColorSpace resolvedColorSpace = (colorSpace == TextureColorSpace::Auto)
                ? ResolveAutoColorSpace(name, path)
                : colorSpace;
            const std::string cacheKey = MakeTextureCacheKey(name, resolvedColorSpace);
            auto it = nameToHandle_.find(cacheKey);
            if (it != nameToHandle_.end()) {
                return it->second;
            }

            int handle = CreateTextureFromFile(path, resolvedColorSpace);
            if (handle >= 0) {
                nameToHandle_[cacheKey] = handle;
            }
            return handle;
        }

        int DxTextureManager::LoadTextureSrgb(const std::string& name, const std::string& path)
        {
            return LoadTextureWithColorSpace(name, path, TextureColorSpace::Srgb);
        }

        int DxTextureManager::LoadTextureLinear(const std::string& name, const std::string& path)
        {
            return LoadTextureWithColorSpace(name, path, TextureColorSpace::Linear);
        }

        int DxTextureManager::LoadCubemap(const std::string& name, const std::string& path, TextureColorSpace colorSpace)
        {
            EnsureInit();
            const std::string cacheKey = MakeTextureCacheKey("cube:" + name, colorSpace);
            auto it = nameToHandle_.find(cacheKey);
            if (it != nameToHandle_.end()) {
                return it->second;
            }

            int handle = CreateCubemapFromFile(path, colorSpace);
            if (handle >= 0) {
                nameToHandle_[cacheKey] = handle;
            }
            return handle;
        }

        int DxTextureManager::CreateTextureFromFile(const std::string& path, TextureColorSpace colorSpace)
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

            const GFX::DescriptorSlot slot = descriptorAllocator_.Allocate();
            if (!slot.IsValid()) {
                OutputDebugStringA("DxTextureManager - out of user texture descriptor slots.\n");
                return -1;
            }

            const int handle = static_cast<int>(slot.index - GFX::DESCRIPTOR::kUserSrvBegin);
            if (handle < 0 || handle >= static_cast<int>(textures_.size())) {
                descriptorAllocator_.Free(slot);
                return -1;
            }

            textures_[handle] = texResource;
            dimensions_[handle] = TextureDimension::Texture2D;

            const DXGI_FORMAT resourceFormat = texResource->GetDesc().Format;
            const DXGI_FORMAT srvFormat = ResolveSrvFormat(resourceFormat, colorSpace);

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = srvFormat;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;

            device->CreateShaderResourceView(
                texResource.Get(), &srvDesc, srvCpu_[handle]);

            return handle;
        }

        int DxTextureManager::CreateCubemapFromFile(const std::string& path, TextureColorSpace colorSpace)
        {
            auto* device = context_.device;
            auto* queue = context_.queue;
            if (!device || !queue || !uploadAllocator_ || !uploadCmdList_ || !uploadFence_ || !uploadFenceEvent_) {
                HIKARI_LOG_ERROR("[DxTextureManager][Cubemap][ERROR] LoadCubemap received invalid D3D12 context.");
                return -1;
            }

            wchar_t wpath[260]{};
            mbstowcs_s(nullptr, wpath, path.c_str(), _TRUNCATE);

            DirectX::TexMetadata metadata{};
            DirectX::ScratchImage image{};
            HRESULT hr = DirectX::LoadFromDDSFile(wpath, DirectX::DDS_FLAGS_NONE, &metadata, image);
            if (FAILED(hr)) {
                std::ostringstream oss;
                oss << "[DxTextureManager][Cubemap][ERROR] LoadCubemap failed. path=" << path
                    << " hr=0x" << std::hex << static_cast<unsigned long>(hr);
                HIKARI_LOG_ERROR(oss.str());
                return -1;
            }

            if (!metadata.IsCubemap()) {
                std::ostringstream oss;
                oss << "[DxTextureManager][Cubemap][ERROR] Resource is not a cubemap texture. path=" << path
                    << " arraySize=" << metadata.arraySize;
                HIKARI_LOG_ERROR(oss.str());
                return -1;
            }

            Microsoft::WRL::ComPtr<ID3D12Resource> texResource;
            hr = DirectX::CreateTexture(device, metadata, texResource.GetAddressOf());
            if (FAILED(hr) || !texResource) {
                std::ostringstream oss;
                oss << "[DxTextureManager][Cubemap][ERROR] CreateTexture failed. path=" << path
                    << " hr=0x" << std::hex << static_cast<unsigned long>(hr);
                HIKARI_LOG_ERROR(oss.str());
                return -1;
            }

            std::vector<D3D12_SUBRESOURCE_DATA> subresources;
            DirectX::PrepareUpload(device, image.GetImages(), image.GetImageCount(), metadata, subresources);
            if (subresources.empty()) {
                HIKARI_LOG_ERROR("[DxTextureManager][Cubemap][ERROR] PrepareUpload returned no subresources.");
                return -1;
            }

            const UINT64 uploadSize = GetRequiredIntermediateSize(texResource.Get(), 0, static_cast<UINT>(subresources.size()));
            auto uploadHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto uploadDesc = CD3DX12_RESOURCE_DESC::Buffer(uploadSize);
            Microsoft::WRL::ComPtr<ID3D12Resource> uploadResource;
            hr = device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &uploadDesc,
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(uploadResource.GetAddressOf()));
            if (FAILED(hr)) {
                std::ostringstream oss;
                oss << "[DxTextureManager][Cubemap][ERROR] Create upload resource failed. hr=0x"
                    << std::hex << static_cast<unsigned long>(hr);
                HIKARI_LOG_ERROR(oss.str());
                return -1;
            }

            hr = uploadAllocator_->Reset();
            assert(SUCCEEDED(hr));
            hr = uploadCmdList_->Reset(uploadAllocator_.Get(), nullptr);
            assert(SUCCEEDED(hr));

            UpdateSubresources(uploadCmdList_.Get(), texResource.Get(), uploadResource.Get(), 0, 0, static_cast<UINT>(subresources.size()), subresources.data());
            auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
                texResource.Get(),
                D3D12_RESOURCE_STATE_COPY_DEST,
                D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            uploadCmdList_->ResourceBarrier(1, &barrier);

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

            const DXGI_FORMAT srvFormat = ResolveSrvFormat(texResource->GetDesc().Format, colorSpace);
            return RegisterCubeFromResourceAs(texResource.Get(), srvFormat);
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

            const GFX::DescriptorSlot slot = descriptorAllocator_.Allocate();
            if (!slot.IsValid()) {
                OutputDebugStringA("DxTextureManager - out of user texture descriptor slots.\n");
                return -1;
            }

            const int handle = static_cast<int>(slot.index - GFX::DESCRIPTOR::kUserSrvBegin);
            if (handle < 0 || handle >= static_cast<int>(textures_.size())) {
                descriptorAllocator_.Free(slot);
                return -1;
            }

            textures_[handle] = resource;
            dimensions_[handle] = TextureDimension::Texture2D;

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

        int DxTextureManager::RegisterCubeFromResourceAs(ID3D12Resource* resource, DXGI_FORMAT srvFormat)
        {
            EnsureInit();
            if (!resource) {
                return -1;
            }

            auto desc = resource->GetDesc();
            if (desc.DepthOrArraySize < 6) {
                std::ostringstream oss;
                oss << "[DxTextureManager][Cubemap][ERROR] Resource is not a cubemap texture. depthOrArraySize="
                    << desc.DepthOrArraySize;
                HIKARI_LOG_ERROR(oss.str());
                return -1;
            }

            auto* device = context_.device;
            const GFX::DescriptorSlot slot = descriptorAllocator_.Allocate();
            if (!slot.IsValid()) {
                OutputDebugStringA("DxTextureManager - out of user texture descriptor slots.\n");
                return -1;
            }

            const int handle = static_cast<int>(slot.index - GFX::DESCRIPTOR::kUserSrvBegin);
            if (handle < 0 || handle >= static_cast<int>(textures_.size())) {
                descriptorAllocator_.Free(slot);
                return -1;
            }

            textures_[handle] = resource;
            dimensions_[handle] = TextureDimension::TextureCube;

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
            srvDesc.Format = srvFormat;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.TextureCube.MostDetailedMip = 0;
            srvDesc.TextureCube.MipLevels = desc.MipLevels;
            srvDesc.TextureCube.ResourceMinLODClamp = 0.0f;

            device->CreateShaderResourceView(resource, &srvDesc, srvCpu_[handle]);
            return handle;
        }

        void DxTextureManager::ReleaseTexture(int handle)
        {
            if (!initialized_ || handle < 0 || handle >= static_cast<int>(textures_.size())) {
                return;
            }

            if (!textures_[handle]) {
                return;
            }

            textures_[handle].Reset();
            dimensions_[handle] = TextureDimension::Texture2D;

            for (auto it = nameToHandle_.begin(); it != nameToHandle_.end();) {
                if (it->second == handle) {
                    it = nameToHandle_.erase(it);
                }
                else {
                    ++it;
                }
            }

            descriptorAllocator_.Free({
                GFX::DESCRIPTOR::kUserSrvBegin + static_cast<UINT>(handle)
            });
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

        TextureDimension DxTextureManager::GetTextureDimension(int handle) {
            if (!initialized_ || handle < 0 || handle >= static_cast<int>(dimensions_.size())) {
                return TextureDimension::Texture2D;
            }
            return dimensions_[handle];
        }


    } // namespace DXTEX
} // namespace HIKARI
