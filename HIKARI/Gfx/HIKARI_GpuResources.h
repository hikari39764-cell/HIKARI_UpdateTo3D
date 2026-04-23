#pragma once

#include <cstdint>
#include <string>

#include <d3d12.h>

namespace HIKARI::GpuResources {

    uint32_t LoadTexture(const std::string& name, const std::string& sourcePath);
    void GetTextureSize(uint32_t gpuTextureId, uint32_t& outWidth, uint32_t& outHeight);

    ID3D12DescriptorHeap* GetSrvHeap();
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle(uint32_t gpuTextureId);

} // namespace HIKARI::GpuResources
