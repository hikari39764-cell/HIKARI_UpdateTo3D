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
    };

    void Reset();
    void SubmitStaticMesh(const ModelAsset& asset, const Transform3D& transform, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4 (&materialFxParamValues)[4], bool materialFxValuesInitialized);
    void SubmitSkinnedMesh(const ModelAsset& asset, const Transform3D& transform, const std::vector<MATH::Mat4>& jointPalette, const std::string& materialFxProfileId, uint32_t postGroupMask, const DirectX::XMFLOAT4 (&materialFxParamValues)[4], bool materialFxValuesInitialized);
    void RenderAll(const Camera3D& camera, const SceneEnvironment& environment);
    const MeshRendererDebugStats& GetDebugStats();

} // namespace HIKARI::MESHRENDERER
