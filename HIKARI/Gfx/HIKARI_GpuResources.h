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

    struct MaterialSrvBlock {
        D3D12_GPU_DESCRIPTOR_HANDLE gpuStart{};
        bool valid = false;
    };

    uint32_t LoadTexture(const std::string& name, const std::string& sourcePath);
    void GetTextureSize(uint32_t gpuTextureId, uint32_t& outWidth, uint32_t& outHeight);

    uint32_t UploadStaticMesh(const std::string& name, const std::vector<ASSET::MeshAsset::Vertex>& vertices, const std::vector<uint32_t>& indices);
    StaticMeshViews GetStaticMeshViews(uint32_t gpuMeshId);

    ID3D12DescriptorHeap* GetSrvHeap();
    ID3D12DescriptorHeap* GetMaterialSrvHeap();
    D3D12_GPU_DESCRIPTOR_HANDLE GetSrvGpuHandle(uint32_t gpuTextureId);
    MaterialSrvBlock AllocateMaterialSrvBlock(uint32_t baseColorTex, uint32_t normalTex, uint32_t ormTex, uint32_t emissiveTex);
    void ResetMaterialSrvAllocator();

} // namespace HIKARI::GpuResources
