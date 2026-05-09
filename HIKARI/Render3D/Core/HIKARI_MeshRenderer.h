#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>
#include <DirectXMath.h>
#include "Render3D/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_ModelAsset.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/HIKARI_SceneEnvironment.h"
#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI::MESHRENDERER {

    struct MeshRendererDebugStats {
        size_t skinnedGpuDrawCount = 0;
        size_t skinnedFallbackCount = 0;
        size_t uploadedJointCount = 0;
        size_t maxJointCount = 0;
        size_t lastSkinnedVertexCount = 0;
        size_t staticDrawItemCount = 0;
        size_t skinnedDrawItemCount = 0;
        size_t primitiveMeshCacheHitCount = 0;
        size_t primitiveMeshCacheMissCount = 0;
        size_t primitiveSkinnedMeshCacheHitCount = 0;
        size_t primitiveSkinnedMeshCacheMissCount = 0;
        size_t materialTextureCacheHitCount = 0;
        size_t materialTextureCacheMissCount = 0;
        size_t psoCacheHitCount = 0;
        size_t psoCacheMissCount = 0;
        size_t materialFxProfileCacheHitCount = 0;
        size_t materialFxProfileCacheMissCount = 0;
        size_t materialFxProfileCacheFailCount = 0;
        size_t normalTextureCacheHitCount = 0;
        size_t normalTextureCacheMissCount = 0;
        size_t normalMappedPrimitiveCount = 0;
        size_t normalMapFallbackCount = 0;
        bool directionalEnabled = false;
        float directionalIntensity = 0.0f;
        float ambientIntensity = 0.0f;
        size_t pointLightTotalCount = 0;
        size_t pointLightUploadedCount = 0;
        size_t pointLightClampedCount = 0;
        float specularIntensity = 0.0f;
        float specularPower = 0.0f;
    };

    void Reset();
    void SubmitStaticMesh(const ModelAsset& asset, const Transform3D& transform, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4 (&materialFxParamValues)[4], bool materialFxValuesInitialized);
    void SubmitSkinnedMesh(const ModelAsset& asset, const Transform3D& transform, const std::vector<MATH::Mat4>& jointPalette, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4 (&materialFxParamValues)[4], bool materialFxValuesInitialized);
    void RenderAll(const Camera3D& camera, const SceneEnvironment& environment);
    const MeshRendererDebugStats& GetDebugStats();

} // namespace HIKARI::MESHRENDERER
