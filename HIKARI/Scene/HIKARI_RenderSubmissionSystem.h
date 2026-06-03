#pragma once

#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Render3D/Runtime/HIKARI_StaticDrawRecordCache.h"
#include "Render3D/Runtime/HIKARI_StaticDrawRecordSubmitter.h"
#include "Scene/HIKARI_SceneRenderCacheSync.h"
#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {

    class Camera3D;

    struct RenderSubmissionDebugStats {
        int scannedModelCount = 0;
        int hiddenModelCount = 0;
        int submittedModelCount = 0;
        int culledModelCount = 0;
        int missingBoundsCount = 0;
        int skinnedCullSkippedCount = 0;
        int fallbackWireCount = 0;
        int staticCachedForwardSkipCount = 0;
        int staticCachedFallbackCount = 0;
        int staticCachedCandidateCount = 0;
        int staticCachedCulledRecordCount = 0;
        int staticCachedSubmittedRecordCount = 0;
        bool frustumCullingEnabled = false;
    };

    struct RenderSubmissionOptions {
        bool useStaticDrawRecordCache = false;
    };

    class RenderSubmissionSystem final : public ISystem {
    public:
        std::string_view GetName() const override { return "RenderSubmissionSystem"; }

        void PreRender(World& world, const FrameContext& frame) override;

        static void SetActiveRenderCamera(const Camera3D* camera);
        static const RenderSubmissionDebugStats& GetDebugStats();
        static void SetUseStaticDrawRecordCache(bool enabled);
        static bool IsUseStaticDrawRecordCacheEnabled();
        static const RENDER3D::RUNTIME::SceneRenderCache& GetSceneRenderCache();
        static const RENDER3D::RUNTIME::SceneRenderCache::Stats& GetSceneRenderCacheStats();
        static const RENDER3D::RUNTIME::StaticDrawRecordCache& GetStaticDrawRecordCache();
        static const RENDER3D::RUNTIME::StaticDrawRecordCache::Stats& GetStaticDrawRecordCacheStats();
        static const RENDER3D::RUNTIME::StaticDrawRecordSubmitStats& GetStaticDrawRecordSubmitStats();

    private:
        static RenderSubmissionDebugStats sDebugStats_;
        static RenderSubmissionOptions sOptions_;
        static const Camera3D* sActiveRenderCamera_;
        static RENDER3D::RUNTIME::SceneRenderCache sSceneRenderCache_;
        static RENDER3D::RUNTIME::StaticDrawRecordCache sStaticDrawRecordCache_;
        static RENDER3D::RUNTIME::StaticRecordSubmitOptions sStaticRecordSubmitOptions_;
        static RENDER3D::RUNTIME::StaticDrawRecordSubmitter sStaticDrawRecordSubmitter_;
        static RENDER3D::RUNTIME::StaticDrawRecordSubmitStats sStaticDrawRecordSubmitStats_;
        static SceneRenderCacheSync sSceneRenderCacheSync_;
    };

} // namespace HIKARI
