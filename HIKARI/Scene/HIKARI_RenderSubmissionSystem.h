#pragma once

#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
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
        bool frustumCullingEnabled = false;
    };

    class RenderSubmissionSystem final : public ISystem {
    public:
        std::string_view GetName() const override { return "RenderSubmissionSystem"; }

        void PreRender(World& world, const FrameContext& frame) override;

        static void SetActiveRenderCamera(const Camera3D* camera);
        static const RenderSubmissionDebugStats& GetDebugStats();
        static const RENDER3D::RUNTIME::SceneRenderCache& GetSceneRenderCache();
        static const RENDER3D::RUNTIME::SceneRenderCache::Stats& GetSceneRenderCacheStats();

    private:
        static RenderSubmissionDebugStats sDebugStats_;
        static const Camera3D* sActiveRenderCamera_;
        static RENDER3D::RUNTIME::SceneRenderCache sSceneRenderCache_;
        static SceneRenderCacheSync sSceneRenderCacheSync_;
    };

} // namespace HIKARI
