#pragma once
#include "Gfx/HIKARI_DescriptorAllocator.h"
#include "Gfx/HIKARI_DescriptorHeapLayout.h"
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

        enum class TextureDimension {
            Texture2D,
            TextureCube
        };

        class DxTextureManager {
        public:
            static void Init(const GFX::Context& ctx, int maxTextures = GFX::DESCRIPTOR::kUserSrvCount);
            static void Finalize();
            static void UpdateContext(const GFX::Context& ctx);

            static int LoadTexture(const std::string& name, const std::string& path);
            static int LoadTextureWithColorSpace(const std::string& name, const std::string& path, TextureColorSpace colorSpace);
            static int LoadTextureSrgb(const std::string& name, const std::string& path);
            static int LoadTextureLinear(const std::string& name, const std::string& path);
            static int LoadCubemap(const std::string& name, const std::string& path, TextureColorSpace colorSpace = TextureColorSpace::Linear);
            static int RegisterFromResource(ID3D12Resource* resource);
            static int RegisterFromResourceAs(ID3D12Resource* resource, DXGI_FORMAT srvFormat);
            static int RegisterCubeFromResourceAs(ID3D12Resource* resource, DXGI_FORMAT srvFormat);
            // Releases the texture handle and returns its descriptor slot to the allocator.
            // Current limitation:
            //   Call this only when the GPU is guaranteed not to be using the texture
            //   anymore, such as during shutdown, scene unload after GPU idle, or explicit
            //   resource rebuild points guarded by synchronization.
            // TODO: Replace immediate release with deferred GPU-safe release.
            static void ReleaseTexture(int handle);
            // Defers releasing the texture resource and descriptor slot until the GPU fence
            // confirms the current frame no longer uses them.
            static void ReleaseTextureDeferred(int handle);
            static UINT GetUsedDescriptorCount();
            static UINT GetFreeDescriptorCount();
            static UINT GetMaxDescriptorCount();
            static D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle(int handle);
            static ID3D12DescriptorHeap* GetSrvHeap();
            static void GetTextureSize(int handle, UINT& outWidth, UINT& outHeight);
            static TextureDimension GetTextureDimension(int handle);

        private:
            static void EnsureInit();
            static int CreateTextureFromFile(const std::string& path, TextureColorSpace colorSpace = TextureColorSpace::Auto);
            static int CreateCubemapFromFile(const std::string& path, TextureColorSpace colorSpace = TextureColorSpace::Linear);
            static void CompleteDeferredRelease(int handle, GFX::DescriptorSlot slot);

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
            static std::vector<TextureDimension> dimensions_;
            static std::vector<D3D12_CPU_DESCRIPTOR_HANDLE> srvCpu_;
            static std::vector<D3D12_GPU_DESCRIPTOR_HANDLE> srvGpu_;
            static std::vector<bool> pendingRelease_;
            static std::unordered_map<std::string, int> nameToHandle_;
            static GFX::DescriptorAllocator descriptorAllocator_;
        };

    } // namespace DXTEX
} // namespace HIKARI
