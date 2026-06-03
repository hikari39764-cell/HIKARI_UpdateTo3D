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

namespace HIKARI::SHADOW {

    struct ShadowMapDebugStats {
        bool enabled = false;
        uint32_t resolution = 0;
        size_t submittedCasterCount = 0;
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
