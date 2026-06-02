#pragma once

#include <cstddef>
#include <cstdint>

#include "Render3D/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/HIKARI_SceneEnvironment.h"
#include "HIKARI_ModelRenderItem.h"

namespace HIKARI::MODELRENDERER {

    struct ModelRendererDebugStats {
        size_t skinnedNodeCount = 0;
        size_t builtPaletteCount = 0;
        size_t totalJointMatrixCount = 0;
        size_t submittedModelItemCount = 0;
        size_t structuredModelCount = 0;
        size_t animatedLocalBuildCount = 0;
        size_t sampledChannelCount = 0;
        size_t sampledKeySearchCount = 0;
        size_t nodeGlobalMatrixBuildCount = 0;
        size_t nodeGlobalMatrixCount = 0;
        size_t jointPaletteBuildCount = 0;
        size_t jointPaletteMatrixCount = 0;
        size_t expandedMeshCacheHitCount = 0;
        size_t expandedMeshCacheMissCount = 0;
        size_t skeletonDebugLineCount = 0;
        size_t poseCacheHitCount = 0;
        size_t poseCacheMissCount = 0;
        size_t poseUpdatedCount = 0;
        size_t poseReusedCount = 0;
        size_t lodNearCount = 0;
        size_t lodMidCount = 0;
        size_t lodFarCount = 0;
        size_t lodVeryFarCount = 0;
        size_t jointPaletteCacheHitCount = 0;
        size_t jointPaletteCacheMissCount = 0;
        size_t structuredNodeSubmittedCount = 0;
        size_t structuredNodeCulledCount = 0;
        size_t structuredCullBoundsMissingCount = 0;
        int lastSkinIndex = -1;
        size_t lastPaletteJointCount = 0;
        bool hasFirstJointMatrix = false;
        MATH::Mat4 firstJointMatrix{};
    };

    void Reset();
    void SubmitModel(const ModelRenderItem& item);
    void RenderAll(const Camera3D& camera, const SceneEnvironment& environment);
    void RenderOpaqueForReflectionProbeCapture(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        uint32_t width,
        uint32_t height);
    const ModelRendererDebugStats& GetDebugStats();

}
