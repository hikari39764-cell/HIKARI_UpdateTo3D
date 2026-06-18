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
    struct SurfaceDrawPacket;
    struct SurfaceDrawCommand;
}

namespace HIKARI::RENDER3D::GPUDRIVEN {
    class SurfaceIndirectDrawBuffer;
    class SurfaceGpuSceneFrameBuffer;
}

namespace HIKARI::SHADOW::PACKET {

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

    using ResolveShadowPacketMeshFn = Mesh* (*)(const MeshPrimitive& primitive);

    struct ShadowPacketExecutorContext {
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
        ResolveShadowPacketMeshFn resolveStaticMesh = nullptr;
    };

    struct ShadowPacketDrawResult {
        size_t submittedPacketCount = 0;
        size_t skippedPacketCount = 0;
        size_t drawCallCount = 0;
        size_t instancedDrawCount = 0;
        size_t instancedPacketCount = 0;
        size_t maxInstanceCount = 0;
        size_t commandCount = 0;
        size_t singlePacketCommandCount = 0;
        size_t maxCommandPacketCount = 0;
        size_t indirectDrawCount = 0;
        size_t indirectPacketCount = 0;
        size_t indirectBatchCount = 0;
        size_t indirectSavedSubmitCount = 0;
        size_t indirectMaxBatchCommandCount = 0;
        size_t indirectFallbackCommandCount = 0;
    };

    bool InitializeShadowPacketExecutor(ID3D12Device* device);
    void ResetShadowPacketExecutor();
    bool PrepareShadowPacketIndirectDrawBindings(
        const ShadowPacketExecutorContext& ctx,
        const RENDER3D::RUNTIME::SurfaceDrawPacket* packets,
        size_t packetCount,
        const uint32_t* executablePacketIndices,
        size_t executablePacketIndexCount,
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>& commands);

    ShadowPacketDrawResult DrawShadowPacketCommands(
        const ShadowPacketExecutorContext& ctx,
        const RENDER3D::RUNTIME::SurfaceDrawPacket* packets,
        size_t packetCount,
        const uint32_t* executablePacketIndices,
        size_t executablePacketIndexCount,
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>& commands,
        size_t& objectIndex);

} // namespace HIKARI::SHADOW::PACKET
