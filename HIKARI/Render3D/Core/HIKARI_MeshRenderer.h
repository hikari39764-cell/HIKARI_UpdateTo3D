#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <DirectXMath.h>
#include "Render3D/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_ModelAsset.h"
#include "Render3D/HIKARI_SceneEnvironment.h"
#include "Render3D/HIKARI_Transform3D.h"
#include "Render3D/Core/HIKARI_MeshRendererTypes.h"
#include <Vfx/Common/HIKARI_FxTypes.h>

namespace HIKARI::RENDER3D {
    class RenderQueue;
    namespace RUNTIME {
        class SurfaceDrawPacketBuilder;
        struct SurfaceDrawCommand;
    }
    namespace SCREENSPACE {
        class SceneGeometryBuffer;
    }
}

namespace HIKARI::MESHRENDERER {

    void Reset();
    void SubmitStaticMesh(const ModelAsset& asset, const Transform3D& transform, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4 (&materialFxParamValues)[VFX::kMaterialFxUserCount], bool materialFxValuesInitialized, bool receiveShadow = true, MeshRenderDebugMode renderDebugMode = MeshRenderDebugMode::Normal, const Material* materialOverride = nullptr);
    void SubmitStaticSubmesh(const ModelAsset& asset, const Transform3D& transform, uint32_t meshIndex, uint32_t primitiveIndex, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4 (&materialFxParamValues)[VFX::kMaterialFxUserCount], bool materialFxValuesInitialized, bool receiveShadow = true, MeshRenderDebugMode renderDebugMode = MeshRenderDebugMode::Normal, const Material* materialOverride = nullptr);
    void SubmitSkinnedMesh(const ModelAsset& asset, const Transform3D& transform, const std::vector<MATH::Mat4>& jointPalette, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4 (&materialFxParamValues)[VFX::kMaterialFxUserCount], bool materialFxValuesInitialized, bool receiveShadow = true, MeshRenderDebugMode renderDebugMode = MeshRenderDebugMode::Normal, const Material* materialOverride = nullptr);
    void SetSurfaceDrawPacketExecutionPlans(
        const RENDER3D::RUNTIME::SurfaceDrawPacketBuilder* builder,
        const std::vector<uint32_t>* opaqueExecutablePacketIndices,
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* opaqueExecutableCommands,
        const std::vector<uint32_t>* transparentExecutablePacketIndices,
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* transparentExecutableCommands);
    bool HasSubmittedItems();
    bool BeginFrame(const Camera3D& camera, const SceneEnvironment& environment);
    bool BeginFrame(const Camera3D& camera, const SceneEnvironment& environment, uint32_t screenWidth, uint32_t screenHeight);
    const RENDER3D::RenderQueue& BuildRenderQueue();
    const CameraCB* GetCameraConstants();
    bool RenderGeometryBufferPass(const RENDER3D::RenderQueue& queue, RENDER3D::SCREENSPACE::SceneGeometryBuffer& geometryBuffer);
    bool RenderForwardOpaquePass(const RENDER3D::RenderQueue& queue, D3D12_GPU_DESCRIPTOR_HANDLE ssaoSrv, int fallbackAoTextureHandle);
    bool RenderForwardTransparentPass(const RENDER3D::RenderQueue& queue, D3D12_GPU_DESCRIPTOR_HANDLE ssaoSrv, int fallbackAoTextureHandle);
    bool RenderDepthAwarePass(const RENDER3D::RenderQueue& queue, D3D12_GPU_DESCRIPTOR_HANDLE ssaoSrv, int fallbackAoTextureHandle);
    void SetAmbientOcclusionRuntimeEnabled(bool enabled);
    void EndFrame();
    void RenderAll(const Camera3D& camera, const SceneEnvironment& environment);
    const MeshRendererDebugStats& GetDebugStats();

} // namespace HIKARI::MESHRENDERER
