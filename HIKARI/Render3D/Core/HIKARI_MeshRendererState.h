#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include <d3d12.h>
#include <wrl/client.h>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Core/HIKARI_MeshMaterialResolver.h"
#include "Render3D/Core/HIKARI_MeshPrimitiveCache.h"
#include "Render3D/Cluster/HIKARI_ClusterGpuCullingPass.h"
#include "Render3D/Core/HIKARI_MeshRendererPso.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/GpuDriven/HIKARI_ClusterGpuDrivenProducerAdapter.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenFrame.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenLayer.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/GpuDriven/HIKARI_SurfaceGpuSceneFrameBuffer.h"
#include "Render3D/GpuDriven/CommandStream/HIKARI_GpuTraditionalCommandStreamBuffer.h"
#include "Render3D/Meshlet/HIKARI_MeshletRenderBackend.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI::MESHRENDERER {

    struct MeshRendererFrameResources {
        Microsoft::WRL::ComPtr<ID3D12Resource> cameraCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> cullingCameraCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> objectCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> objectDataUploadBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> objectDataBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> materialDataUploadBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> materialDataBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> lightCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> shadowCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> skyEnvironmentCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> jointPaletteCB;

        CameraCB* cameraMapped = nullptr;
        CameraCB* cullingCameraMapped = nullptr;
        ObjectCB* objectMapped = nullptr;
        ObjectGpuData* objectDataMapped = nullptr;
        MaterialGpuData* materialDataMapped = nullptr;
        LightCB* lightMapped = nullptr;
        ShadowCB* shadowMapped = nullptr;
        SkyEnvironmentCB* skyEnvironmentMapped = nullptr;
        JointPaletteCB* jointPaletteMapped = nullptr;

        D3D12_CPU_DESCRIPTOR_HANDLE objectDataSrvCpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE objectDataSrvGpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE materialDataSrvCpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE materialDataSrvGpu{};
        D3D12_RESOURCE_STATES objectDataState = D3D12_RESOURCE_STATE_COMMON;
        D3D12_RESOURCE_STATES materialDataState = D3D12_RESOURCE_STATE_COMMON;
    };

    struct MeshRendererState {
        bool initialized = false;

        MeshPipelineStore pipelines;
        std::array<MeshRendererFrameResources, GFX::kFrameResourceCount> frameResources{};
        uint32_t activeFrameResourceIndex = 0;

        Microsoft::WRL::ComPtr<ID3D12Resource> cameraCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> cullingCameraCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> objectCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> objectDataBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> materialDataBuffer;
        Microsoft::WRL::ComPtr<ID3D12Resource> lightCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> shadowCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> skyEnvironmentCB;
        Microsoft::WRL::ComPtr<ID3D12Resource> jointPaletteCB;

        CameraCB* cameraMapped = nullptr;
        CameraCB* cullingCameraMapped = nullptr;
        ObjectCB* objectMapped = nullptr;
        ObjectGpuData* objectDataMapped = nullptr;
        MaterialGpuData* materialDataMapped = nullptr;
        LightCB* lightMapped = nullptr;
        ShadowCB* shadowMapped = nullptr;
        SkyEnvironmentCB* skyEnvironmentMapped = nullptr;
        JointPaletteCB* jointPaletteMapped = nullptr;

        MeshRendererDebugStats debugStats;
        CameraCB frozenCullingCamera{};
        bool freezeGpuDrivenCullingCamera = false;
        bool frozenCullingCameraValid = false;
        uint32_t frozenCullingCameraWidth = 0;
        uint32_t frozenCullingCameraHeight = 0;

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
        size_t frameObjectIndex = 0;
        MaterialDataFrameTable materialDataFrameTable{};
        D3D12_CPU_DESCRIPTOR_HANDLE objectDataSrvCpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE objectDataSrvGpu{};
        D3D12_CPU_DESCRIPTOR_HANDLE materialDataSrvCpu{};
        D3D12_GPU_DESCRIPTOR_HANDLE materialDataSrvGpu{};
        RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBuffer surfaceGpuSceneBuffer{};
        RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamBuffer traditionalCommandStreamBuffer{};
        RENDER3D::GPUDRIVEN::GpuDrivenFrame gpuDrivenFrame{};
        RENDER3D::GPUDRIVEN::GpuDrivenLayer gpuDrivenLayer{};
        RENDER3D::CLUSTER::ClusterGpuCullingPass clusterGpuCullingPass{};
        RENDER3D::GPUDRIVEN::ClusterGpuDrivenProducerAdapter clusterGpuDrivenProducer{};
        RENDER3D::MESHLET::MeshletRenderBackend meshletRenderBackend{};

        RENDER3D::GPUDRIVEN::GpuDrivenSceneSource gpuDrivenSceneSource{};
        RENDER3D::GPUDRIVEN::GpuDrivenSceneResidency gpuDrivenSceneResidency{};

        float elapsedTimeSec = 0.0f;
    };

} // namespace HIKARI::MESHRENDERER
