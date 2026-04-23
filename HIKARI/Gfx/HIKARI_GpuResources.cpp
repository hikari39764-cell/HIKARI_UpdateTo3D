#include "HIKARI_GpuResources.h"

#include "Render2D/HIKARI_DxTexture.h"

namespace HIKARI::GpuResources {

    uint32_t LoadTexture(const std::string& name, const std::string& sourcePath) {
        const int handle = DXTEX::DxTextureManager::LoadTexture(name, sourcePath);
        return handle < 0 ? 0u : static_cast<uint32_t>(handle + 1);
    }

    void GetTextureSize(uint32_t gpuTextureId, uint32_t& outWidth, uint32_t& outHeight) {
        outWidth = 0;
        outHeight = 0;
        if (gpuTextureId == 0) {
            return;
        }
        UINT w = 0;
        UINT h = 0;
        DXTEX::DxTextureManager::GetTextureSize(static_cast<int>(gpuTextureId - 1), w, h);
        outWidth = static_cast<uint32_t>(w);
        outHeight = static_cast<uint32_t>(h);
    }

    ID3D12DescriptorHeap* GetSrvHeap() {
        return DXTEX::DxTextureManager::GetSrvHeap();
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle(uint32_t gpuTextureId) {
        if (gpuTextureId == 0) {
            return {};
        }
        return DXTEX::DxTextureManager::GetSrvGpuHandle(static_cast<int>(gpuTextureId - 1));
    }

} // namespace HIKARI::GpuResources
