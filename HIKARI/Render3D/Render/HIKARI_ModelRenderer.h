#pragma once

#include <cstddef>
#include <cstdint>

#include "Render3D/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/HIKARI_SceneEnvironment.h"
#include "Render3D/Runtime/HIKARI_RenderModelCache.h"
#include "HIKARI_ModelRenderItem.h"

namespace HIKARI::MODELRENDERER {

    enum class ModelRendererFrameKind {
        None,
        MainView,
        ReflectionProbeCapture,
        LightProbeCapture,
    };

    const char* ToString(ModelRendererFrameKind kind);

    struct ModelRendererFrameStats {
        uint32_t submittedModelItemCount = 0;
        uint32_t structuredModelCount = 0;
        uint32_t structuredNodeSubmittedCount = 0;
        uint32_t structuredNodeCulledCount = 0;
        uint32_t structuredCullBoundsMissingCount = 0;

        uint32_t renderModelValidRequestCount = 0;
        uint32_t renderModelInvalidRequestCount = 0;
        uint32_t renderModelRequestedSubmeshCount = 0;

        uint32_t skinnedNodeCount = 0;
        uint32_t builtPaletteCount = 0;
        uint32_t totalJointMatrixCount = 0;
        uint32_t skeletonDebugLineCount = 0;

        uint32_t animatedLocalBuildCount = 0;
        uint32_t sampledChannelCount = 0;
        uint32_t sampledKeySearchCount = 0;
        uint32_t nodeGlobalMatrixBuildCount = 0;
        uint32_t nodeGlobalMatrixCount = 0;
        uint32_t jointPaletteBuildCount = 0;
        uint32_t jointPaletteMatrixCount = 0;
        uint32_t lodNearCount = 0;
        uint32_t lodMidCount = 0;
        uint32_t lodFarCount = 0;
        uint32_t lodVeryFarCount = 0;

        int lastSkinIndex = -1;
        uint32_t lastPaletteJointCount = 0;
        bool hasFirstJointMatrix = false;
        MATH::Mat4 firstJointMatrix{};
    };

    struct ModelRendererCacheStats {
        uint32_t renderModelCacheRequestCount = 0;
        uint32_t renderModelCacheHitCount = 0;
        uint32_t renderModelCacheMissCount = 0;
        uint32_t renderModelCacheInvalidCount = 0;
        uint32_t renderModelCachedModelCount = 0;
        uint32_t renderModelCachedSubmeshCount = 0;

        uint32_t poseCacheHitCount = 0;
        uint32_t poseCacheMissCount = 0;
        uint32_t poseUpdatedCount = 0;
        uint32_t poseReusedCount = 0;
        uint32_t expandedMeshCacheHitCount = 0;
        uint32_t expandedMeshCacheMissCount = 0;
        uint32_t jointPaletteCacheHitCount = 0;
        uint32_t jointPaletteCacheMissCount = 0;
    };

    struct ModelRendererDebugStats {
        ModelRendererFrameKind frameKind = ModelRendererFrameKind::None;
        ModelRendererFrameStats frame{};
        ModelRendererCacheStats cache{};
    };

    void Reset();
    void ResetModelRendererFrameStats();
    void BeginModelRendererFrame(ModelRendererFrameKind kind);
    void SubmitModel(const ModelRenderItem& item);
    void RenderAll(const Camera3D& camera, const SceneEnvironment& environment);
    void RenderOpaqueForReflectionProbeCapture(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        uint32_t width,
        uint32_t height,
        ModelRendererFrameKind kind = ModelRendererFrameKind::ReflectionProbeCapture);
    const ModelRendererDebugStats& GetDebugStats();
    RENDER3D::RUNTIME::RenderModelCache& GetRenderModelCache();
    const RENDER3D::RUNTIME::RenderModelCache::Stats& GetRenderModelCacheStats();

}
