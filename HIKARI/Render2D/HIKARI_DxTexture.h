#pragma once
#include "Gfx/HIKARI_GfxContext.h"
#include <d3d12.h>
#include <cstdint>
#include <Windows.h>
#include <wrl.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace HIKARI {
    namespace DXTEX {

        enum class TextureColorSpace {
            Auto,
            Linear,
            Srgb
        };

        class DxTextureManager {
        public:
            static void Init(const GFX::Context& ctx, int maxTextures = 128);
            static void Finalize();
            static void UpdateContext(const GFX::Context& ctx);

            static int LoadTexture(const std::string& name, const std::string& path);
            static int LoadTextureWithColorSpace(const std::string& name, const std::string& path, TextureColorSpace colorSpace);
            static int LoadTextureSrgb(const std::string& name, const std::string& path);
            static int LoadTextureLinear(const std::string& name, const std::string& path);
            static int RegisterFromResource(ID3D12Resource* resource);
            static int RegisterFromResourceAs(ID3D12Resource* resource, DXGI_FORMAT srvFormat);
            static D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle(int handle);
            static ID3D12DescriptorHeap* GetSrvHeap();
            static void GetTextureSize(int handle, UINT& outWidth, UINT& outHeight);

        private:
            static void EnsureInit();
            static int CreateTextureFromFile(const std::string& path, TextureColorSpace colorSpace = TextureColorSpace::Auto);

            static bool initialized_;
            static GFX::Context context_;
            static UINT descriptorSize_;

            static Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> srvHeap_;
            static Microsoft::WRL::ComPtr<ID3D12CommandAllocator> uploadAllocator_;
            static Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> uploadCmdList_;
            static Microsoft::WRL::ComPtr<ID3D12Fence> uploadFence_;
            static HANDLE uploadFenceEvent_;
            static uint64_t uploadFenceValue_;
            static std::vector<Microsoft::WRL::ComPtr<ID3D12Resource>> textures_;
            static std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> srvCpu_;
            static std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> srvGpu_;
            static std::unordered_map<std::string, int> nameToHandle_;
            static int nextIndex_;
        };

    } // namespace DXTEX
} // namespace HIKARI
