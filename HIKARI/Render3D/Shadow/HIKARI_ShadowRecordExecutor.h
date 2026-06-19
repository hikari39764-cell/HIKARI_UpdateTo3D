#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <d3d12.h>

#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI {
    class Mesh;
    class ModelAsset;
    struct MaterialAsset;
    struct MeshPrimitive;
}

namespace HIKARI::RENDER3D::RUNTIME {
    struct SurfaceDrawCommand;
}

namespace HIKARI::RENDER3D::GPUDRIVEN {
    struct GpuSceneSurfaceRecord;
    class SurfaceIndirectDrawBuffer;
    class SurfaceGpuSceneFrameBuffer;
}

namespace HIKARI::SHADOW::RECORD {

    constexpr UINT kShadowStaticRootParamCamera = 0;
    constexpr UINT kShadowStaticRootParamObject = 1;
    constexpr UINT kShadowStaticRootParamBaseColorTexture = 2;
    constexpr UINT kShadowStaticRootParamMaterialData = 3;
    constexpr UINT kShadowStaticRootParamSurfaceGpuScene = 4;
    constexpr UINT kShadowStaticRootParamSurfaceGpuSceneControl = 5;
    constexpr UINT kShadowStaticRootParamTexturePool = 6;
    constexpr UINT kShadowStaticRootParamMaterialIndex = 7;
    constexpr UINT kShadowStaticRootParamObjectData = 8;
    constexpr UINT kShadowStaticRootParamClusterGeometryPool = 9;
    constexpr UINT kShadowStaticRootParamMeshletVisibleRanges = 10;
    constexpr UINT kShadowSkinnedRootParamJointPalette = 11;

    using ResolveShadowRecordMeshFn = Mesh* (*)(const MeshPrimitive& primitive);

    struct ShadowRecordExecutorContext {
        ID3D12GraphicsCommandList* cmd = nullptr;
        ID3D12RootSignature* staticRootSig = nullptr;
        ID3D12PipelineState* staticPso = nullptr;
        D3D12_GPU_VIRTUAL_ADDRESS cameraAddress = 0;
        D3D12_GPU_DESCRIPTOR_HANDLE fallbackBaseColorSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE materialDataSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE surfaceGpuSceneSrv{};
        D3D12_GPU_DESCRIPTOR_HANDLE texturePoolSrv{};
        RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBuffer* surfaceGpuSceneFrameBuffer = nullptr;
        RENDER3D::GPUDRIVEN::SurfaceIndirectDrawBuffer* indirectDrawBuffer = nullptr;
        ResolveShadowRecordMeshFn resolveStaticMesh = nullptr;
    };

    struct ShadowRecordDrawResult {
        size_t submittedRecordCount = 0;
        size_t skippedRecordCount = 0;
        size_t drawCallCount = 0;
        size_t instancedDrawCount = 0;
        size_t instancedRecordCount = 0;
        size_t maxInstanceCount = 0;
        size_t commandCount = 0;
        size_t singleRecordCommandCount = 0;
        size_t maxCommandRecordCount = 0;
        size_t indirectDrawCount = 0;
        size_t indirectRecordCount = 0;
        size_t indirectBatchCount = 0;
        size_t indirectSavedSubmitCount = 0;
        size_t indirectMaxBatchCommandCount = 0;
        size_t indirectFallbackCommandCount = 0;
    };

    bool InitializeShadowRecordExecutor(ID3D12Device* device);
    void ResetShadowRecordExecutor();
    bool PrepareShadowRecordIndirectDrawBindings(
        const ShadowRecordExecutorContext& ctx,
        const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord* records,
        size_t recordCount,
        const uint32_t* executableRecordIndices,
        size_t executableRecordIndexCount,
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>& commands);

    ShadowRecordDrawResult DrawShadowRecordCommands(
        const ShadowRecordExecutorContext& ctx,
        const RENDER3D::GPUDRIVEN::GpuSceneSurfaceRecord* records,
        size_t recordCount,
        const uint32_t* executableRecordIndices,
        size_t executableRecordIndexCount,
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>& commands,
        size_t& objectIndex);

} // namespace HIKARI::SHADOW::RECORD
