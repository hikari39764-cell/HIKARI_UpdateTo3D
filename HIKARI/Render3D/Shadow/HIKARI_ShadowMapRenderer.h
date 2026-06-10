#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>
#include <d3d12.h>

#include "Render3D/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/HIKARI_ModelAsset.h"
#include "Render3D/HIKARI_SceneEnvironment.h"
#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI::RENDER3D::RUNTIME {
    class SurfaceDrawPacketBuilder;
    struct SurfaceDrawCommand;
    struct SurfaceGpuSceneInstance;
}

namespace HIKARI::SHADOW {

    struct ShadowMapDebugStats {
        bool enabled = false;
        uint32_t resolution = 0;
        size_t submittedCasterCount = 0;
        size_t shadowPacketCasterDrawCount = 0;
        size_t shadowPacketSkippedCount = 0;
        size_t shadowPacketCommandCount = 0;
        size_t shadowPacketSingleCommandCount = 0;
        size_t shadowPacketMaxCommandPacketCount = 0;
        size_t shadowPacketDrawCallCount = 0;
        size_t shadowPacketInstancedDrawCount = 0;
        size_t shadowPacketInstancedCasterCount = 0;
        size_t shadowPacketMaxInstanceCount = 0;
        size_t shadowIndirectCapacity = 0;
        size_t shadowIndirectRequestedCommandCount = 0;
        size_t shadowIndirectUploadedCommandCount = 0;
        size_t shadowIndirectOverflowCommandCount = 0;
        size_t shadowIndirectCpuDirectCommandCount = 0;
        size_t shadowIndirectMissingDrawArgsCommandCount = 0;
        size_t shadowIndirectDrawBindingPatchCount = 0;
        size_t shadowIndirectExecutedCommandCount = 0;
        size_t shadowIndirectExecutedPacketCount = 0;
        size_t shadowIndirectBatchSubmitCount = 0;
        size_t shadowIndirectSavedSubmitCount = 0;
        size_t shadowIndirectMaxBatchCommandCount = 0;
        size_t shadowIndirectFallbackCommandCount = 0;
        bool shadowIndirectArgumentBufferReady = false;
        bool shadowIndirectCommandSignatureReady = false;
        size_t shadowGpuSceneCapacity = 0;
        size_t shadowGpuSceneRequestedInstanceCount = 0;
        size_t shadowGpuSceneUploadedInstanceCount = 0;
        size_t shadowGpuSceneOverflowInstanceCount = 0;
        size_t shadowGpuSceneUploadCallCount = 0;
        bool shadowGpuSceneSrvValid = false;
        bool shadowGpuSceneBufferReady = false;
        size_t staticCasterDrawCount = 0;
        size_t skinnedCasterDrawCount = 0;
        size_t alphaMaskCasterDrawCount = 0;
        size_t skippedNoCastShadowCount = 0;
        size_t totalPrimitiveCasterDrawCount = 0;
        size_t shadowMapRecreateCount = 0;
        size_t pcfEnabled = 0;
        float pcfRadius = 0.0f;
        float orthoSize = 0.0f;
        float nearPlane = 0.0f;
        float farPlane = 0.0f;
        float depthBias = 0.0f;
        float normalBias = 0.0f;
        float strength = 0.0f;
    };

    void Reset();
    void BeginFrame(const SceneEnvironment& environment, const Camera3D& camera);
    void SubmitStaticMesh(const ModelAsset& asset, const Transform3D& transform, bool castShadow);
    void SubmitStaticSubmesh(const ModelAsset& asset, const Transform3D& transform, uint32_t meshIndex, uint32_t primitiveIndex, bool castShadow);
    void SubmitSkinnedMesh(const ModelAsset& asset, const Transform3D& transform, const std::vector<MATH::Mat4>& jointPalette, bool castShadow);
    void SetSurfaceDrawPacketExecutionPlan(
        const RENDER3D::RUNTIME::SurfaceDrawPacketBuilder* builder,
        const std::vector<uint32_t>* executablePacketIndices,
        const std::vector<RENDER3D::RUNTIME::SurfaceDrawCommand>* executableCommands,
        const std::vector<RENDER3D::RUNTIME::SurfaceGpuSceneInstance>* gpuSceneInstances);
    void RenderDirectionalShadowMap();

    bool IsDirectionalShadowEnabled();
    const MATH::Mat4& GetDirectionalLightViewProj();
    D3D12_GPU_DESCRIPTOR_HANDLE GetDirectionalShadowSrv();
    uint32_t GetShadowResolution();
    float GetShadowStrength();
    float GetDepthBias();
    float GetNormalBias();
    const ShadowMapDebugStats& GetDebugStats();

} // namespace HIKARI::SHADOW
