#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <d3d12.h>
#include <DirectXMath.h>
#include "Render3D/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_ModelAsset.h"
#include "Render3D/HIKARI_SceneEnvironment.h"
#include "Render3D/HIKARI_Transform3D.h"
#include "Render3D/Core/HIKARI_MeshPassResources.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include <Vfx/Common/HIKARI_FxTypes.h>

namespace HIKARI::RENDER3D {
    class RenderQueue;
    namespace RUNTIME {
        class SurfaceDrawPacketBuilder;
        struct SurfaceDrawCommand;
        struct SurfaceGpuSceneInstance;
    }
    namespace SCREENSPACE {
        class ScreenSpaceGeometryAux;
    }
}

namespace HIKARI::MESHRENDERER {

    void Reset();
    void SubmitStaticMesh(const ModelAsset& asset, const Transform3D& transform, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4 (&materialFxParamValues)[VFX::kMaterialFxUserCount], bool materialFxValuesInitialized, bool receiveShadow = true, MeshRenderDebugMode renderDebugMode = MeshRenderDebugMode::Normal, const Material* materialOverride = nullptr);
    void SubmitStaticSubmesh(const ModelAsset& asset, const Transform3D& transform, uint32_t meshIndex, uint32_t primitiveIndex, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4 (&materialFxParamValues)[VFX::kMaterialFxUserCount], bool materialFxValuesInitialized, bool receiveShadow = true, MeshRenderDebugMode renderDebugMode = MeshRenderDebugMode::Normal, const Material* materialOverride = nullptr);
    void SubmitSkinnedMesh(const ModelAsset& asset, const Transform3D& transform, const std::vector<MATH::Mat4>& jointPalette, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4 (&materialFxParamValues)[VFX::kMaterialFxUserCount], bool materialFxValuesInitialized, bool receiveShadow = true, MeshRenderDebugMode renderDebugMode = MeshRenderDebugMode::Normal, const Material* materialOverride = nullptr);
    void SubmitSkinnedSubmesh(const ModelAsset& asset, const Transform3D& transform, const std::vector<MATH::Mat4>& jointPalette, uint32_t meshIndex, uint32_t primitiveIndex, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4 (&materialFxParamValues)[VFX::kMaterialFxUserCount], bool materialFxValuesInitialized, bool receiveShadow = true, MeshRenderDebugMode renderDebugMode = MeshRenderDebugMode::Normal, const Material* materialOverride = nullptr);
    void SetSurfaceDrawPacketExecutionPlans(
        const RENDER3D::RUNTIME::SurfaceDrawPacketBuilder* builder,
        const std::vector<uint32_t>* opaqueExecutablePacketIndices,
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* opaqueExecutableCommands,
        const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance>* opaqueGpuSceneInstances,
        const std::vector<uint32_t>* depthAwareExecutablePacketIndices,
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* depthAwareExecutableCommands,
        const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance>* depthAwareGpuSceneInstances,
        const std::vector<uint32_t>* transparentExecutablePacketIndices,
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* transparentExecutableCommands,
        const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance>* transparentGpuSceneInstances);
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
    const RENDER3D::RenderQueue& BuildRenderQueue();
    const CameraCB* GetCameraConstants();
    bool RenderGeometryAuxPass(
        const RENDER3D::RenderQueue& queue,
        RENDER3D::SCREENSPACE::ScreenSpaceGeometryAux& geometryAux,
        D3D12_CPU_DESCRIPTOR_HANDLE sceneDsv);
    bool RenderForwardOpaquePass(
        const RENDER3D::RenderQueue& queue,
        const MeshPassResources& passResources);
    bool RenderForwardTransparentPass(
        const RENDER3D::RenderQueue& queue,
        const MeshPassResources& passResources);
    bool HasDepthAwarePassWork(const RENDER3D::RenderQueue& queue);
    bool RenderDepthAwarePass(
        const RENDER3D::RenderQueue& queue,
        const MeshPassResources& passResources);
    void SetAmbientOcclusionRuntimeEnabled(bool enabled);
    void EndFrame();
    void RenderAll(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        RenderDebugView debugView = RenderDebugView::None);
    const MeshRendererDebugStats& GetDebugStats();

} // namespace HIKARI::MESHRENDERER
