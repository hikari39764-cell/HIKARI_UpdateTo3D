#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Render3D/Core/HIKARI_MeshMaterialResolver.h"
#include "Render3D/Core/HIKARI_MeshPrimitiveCache.h"
#include "Render3D/Cluster/HIKARI_ClusterGpuCullingPass.h"
#include "Render3D/Core/HIKARI_MeshRendererFrameResources.h"
#include "Render3D/Core/HIKARI_MeshRendererPso.h"
#include "Render3D/Core/HIKARI_MeshRendererTraditionalIndirectOwner.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include "Render3D/GpuDriven/HIKARI_ClusterGpuDrivenProducerAdapter.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenFrame.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenLayer.h"
#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/GpuDriven/HIKARI_SurfaceGpuSceneFrameBuffer.h"
#include "Render3D/GpuDriven/CommandStream/HIKARI_GpuTraditionalCommandStreamBuffer.h"
#include "Render3D/Material/HIKARI_GpuMaterialRegistry.h"
#include "Render3D/Meshlet/HIKARI_MeshletRenderBackend.h"
#include "Render3D/Resources/HIKARI_RenderResourceHandle.h"

namespace HIKARI::MESHRENDERER {

    struct MeshRendererState {
        bool initialized = false;

        MeshPipelineStore pipelines;
        MeshRendererFrameResourceStore frameResources{};

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
        RENDER3D::MATERIAL::GpuMaterialRegistry gpuMaterialRegistry{};
        std::vector<RENDER3D::MATERIAL::GpuMaterialUploadRange> materialUploadRanges{};
        RENDER3D::GPUDRIVEN::SurfaceGpuSceneFrameBuffer surfaceGpuSceneBuffer{};
        RENDER3D::GPUDRIVEN::GpuTraditionalCommandStreamBuffer traditionalCommandStreamBuffer{};
        MeshRendererTraditionalIndirectOwner traditionalIndirectOwner{};
        RENDER3D::GPUDRIVEN::GpuDrivenFrame gpuDrivenFrame{};
        RENDER3D::GPUDRIVEN::GpuDrivenLayer gpuDrivenLayer{};
        RENDER3D::CLUSTER::ClusterGpuCullingPass clusterGpuCullingPass{};
        RENDER3D::GPUDRIVEN::ClusterGpuDrivenProducerAdapter clusterGpuDrivenProducer{};
        RENDER3D::MESHLET::MeshletRenderBackend meshletRenderBackend{};

        RENDER3D::GPUDRIVEN::GpuDrivenSceneSource gpuDrivenSceneSource{};
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* gpuDrivenSceneSourceIdentity = nullptr;
        RENDER3D::GPUDRIVEN::GpuDrivenSceneResidency gpuDrivenSceneResidency{};

        float elapsedTimeSec = 0.0f;
    };

} // namespace HIKARI::MESHRENDERER
