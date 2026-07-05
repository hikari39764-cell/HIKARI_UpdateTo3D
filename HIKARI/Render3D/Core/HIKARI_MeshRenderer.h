#pragma once
#include <string>
#include <cstdint>
#include <d3d12.h>
#include <DirectXMath.h>
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Core/HIKARI_ModelAsset.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/HIKARI_Transform3D.h"
#include "Render3D/Core/HIKARI_MeshPassResources.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"

namespace HIKARI::RENDER3D {
    namespace GPUDRIVEN {
        struct GpuDrivenSceneSource;
        struct GpuDepthVisibilityStats;
    }
    namespace SCREENSPACE {
        class ScreenSpaceGeometryAux;
    }
}

namespace HIKARI::MESHRENDERER {

    void Reset();
    void InvalidateMaterialFxPipelineCache();
    void SetGpuDrivenSceneSource(
        const RENDER3D::GPUDRIVEN::GpuDrivenSceneSource* source);
    void SetGpuDrivenCullingDebugFreezeEnabled(bool enabled);
    GpuDrivenCullingDebugView GetGpuDrivenCullingDebugView();
    bool IsGpuDrivenCullingDebugFreezeActive();
    bool HasSubmittedItems();
    bool BeginFrame(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        RenderDebugView debugView = RenderDebugView::None);
    bool BeginFrame(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        uint32_t screenWidth,
        uint32_t screenHeight,
        RenderDebugView debugView = RenderDebugView::None);
    const CameraCB* GetCameraConstants();
    const CameraCB* GetGpuDrivenCullingCameraConstants();
    bool RenderGeometryAuxPass(
        RENDER3D::SCREENSPACE::ScreenSpaceGeometryAux& geometryAux,
        D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv);
    bool HasDepthPrepassWork();
    bool RenderDepthPrepass(D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv);
    bool FinalizeGpuDrivenVisibilityWithoutDepth();
    bool FinalizeGpuDrivenVisibilityFromDepth(
        const RENDER3D::GPUDRIVEN::GpuDepthVisibilityStats& depthVisibilityStats);
    bool RenderForwardOpaquePass(
        const MeshPassResources& passResources);
    bool RenderForwardTransparentPass(
        const MeshPassResources& passResources);
    bool HasDepthAwarePassWork();
    bool HasForwardTransparentPassWork();
    bool RenderDepthAwarePass(
        const MeshPassResources& passResources);
    void SetAmbientOcclusionRuntimeEnabled(bool enabled);
    void EndFrame();
    void RenderAll(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        RenderDebugView debugView = RenderDebugView::None);
    const MeshRendererDebugStats& GetDebugStats();

} // namespace HIKARI::MESHRENDERER
