#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <d3d12.h>

#include "Assets/HIKARI_Assets.h"

namespace HIKARI::GpuResources {

    struct StaticMeshViews {
        D3D12_VERTEX_BUFFER_VIEW vbv{};
        D3D12_INDEX_BUFFER_VIEW ibv{};
        uint32_t indexCount = 0;
        bool valid = false;
    };

    uint32_t LoadTexture(const std::string& name, const std::string& sourcePath);
    void GetTextureSize(uint32_t gpuTextureId, uint32_t& outWidth, uint32_t& outHeight);

    uint32_t UploadStaticMesh(const std::string& name, const std::vector<ASSET::MeshAsset::Vertex>& vertices, const std::vector<uint32_t>& indices);
    StaticMeshViews GetStaticMeshViews(uint32_t gpuMeshId);

    ID3D12DescriptorHeap* GetSrvHeap();
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle(uint32_t gpuTextureId);

} // namespace HIKARI::GpuResources
