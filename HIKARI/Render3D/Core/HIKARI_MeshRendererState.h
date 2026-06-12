#pragma once

#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Render3D/Core/HIKARI_MeshMaterialResolver.h"
#include "Render3D/Core/HIKARI_MeshPrimitiveCache.h"
#include "Render3D/Cluster/HIKARI_ClusterDrawExecutor.h"
#include "Render3D/Cluster/HIKARI_ClusterGpuCullingPass.h"
#include "Render3D/Cluster/HIKARI_ClusterMainline.h"
#include "Render3D/Core/HIKARI_MeshRendererPso.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/Core/HIKARI_SurfaceGpuSceneFrameBuffer.h"
#include "Render3D/Core/HIKARI_SurfaceIndirectDrawBuffer.h"
#include "Render3D/Meshlet/HIKARI_MeshletRenderBackend.h"
#include "Render3D/Pipeline/HIKARI_RenderQueue.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI::RENDER3D::RUNTIME {
    class SurfaceDrawPacketBuilder;
    struct SurfaceDrawCommand;
    struct SurfaceGpuSceneInstance;
}

namespace HIKARI::MESHRENDERER {

    struct MeshRendererState {
        bool initialized = false;

        MeshPipelineStore pipelines;

        Microsoft::WRL::ComPtr<ID3D12Resource> cameraCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> objectCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> objectDataBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> materialDataBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> lightCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> shadowCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> skyEnvironmentCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> jointPaletteCB;

        CameraCB* cameraMapped = nullptr;
        ObjectCB* objectMapped = nullptr;
        ObjectGpuData* objectDataMapped = nullptr;
        MaterialGpuData* materialDataMapped = nullptr;
        LightCB* lightMapped = nullptr;
        ShadowCB* shadowMapped = nullptr;
        SkyEnvironmentCB* skyEnvironmentMapped = nullptr;
        JointPaletteCB* jointPaletteMapped = nullptr;

        std::vector<DrawItem> drawItems;
        MeshRendererDebugStats debugStats;

        RENDER3D::TextureResourceHandle fallbackTextureResource{};
        RENDER3D::TextureResourceHandle fallbackNormalTextureResource{};
        RENDER3D::TextureResourceHandle fallbackBlackTextureResource{};
        RENDER3D::TextureResourceHandle fallbackCubeTextureResource{};

        int fallbackTextureHandle = -1;
        int fallbackNormalTextureHandle = -1;
        int fallbackBlackTextureHandle = -1;
        int fallbackCubeTextureHandle = -1;

        MeshPrimitiveCache primitiveCache;
        MeshMaterialResolver materialResolver;
        RENDER3D::RenderQueue renderQueue;
        size_t frameObjectIndex = 0;
        MaterialDataFrameTable materialDataFrameTable{};
        D3D12_CPU_DESCRIPTOR_HANDLE objectDataSrvCpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE objectDataSrvGpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE materialDataSrvCpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE materialDataSrvGpu{};
        RENDER3D::CORE::SurfaceGpuSceneFrameBuffer surfaceGpuSceneBuffer{};
        RENDER3D::CORE::SurfaceIndirectDrawBuffer surfaceIndirectDrawBuffer{};
        RENDER3D::CLUSTER::ClusterGpuCullingPass clusterGpuCullingPass{};
        RENDER3D::CLUSTER::ClusterDrawExecutor clusterDrawExecutor{};
        RENDER3D::CLUSTER::ClusterMainlineFrame staticOpaqueClusterMainlineFrame{};
        RENDER3D::MESHLET::MeshletRenderBackend meshletRenderBackend{};

        const RENDER3D::RUNTIME::SurfaceDrawPacketBuilder* surfacePacketBuilder = nullptr;
        const std::vector<uint32_t>* surfacePacketOpaqueExecutionIndices = nullptr;
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* surfacePacketOpaqueExecutionCommands = nullptr;
        const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance>* surfacePacketOpaqueGpuSceneInstances = nullptr;
        const std::vector<uint32_t>* surfacePacketDepthAwareExecutionIndices = nullptr;
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* surfacePacketDepthAwareExecutionCommands = nullptr;
        const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance>* surfacePacketDepthAwareGpuSceneInstances = nullptr;
        const std::vector<uint32_t>* surfacePacketTransparentExecutionIndices = nullptr;
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* surfacePacketTransparentExecutionCommands = nullptr;
        const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance>* surfacePacketTransparentGpuSceneInstances = nullptr;

        float elapsedTimeSec = 0.0f;
    };

} // namespace HIKARI::MESHRENDERER
