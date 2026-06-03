#pragma once

#include <cstdint>
#include <filesystem>

#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Render3D/Runtime/HIKARI_StaticDrawRecordCache.h"
#include "Render3D/Runtime/HIKARI_StaticDrawRecordSubmitter.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawPacket.h"
#include "Render3D/Cluster/HIKARI_ClusteredGeometryDebug.h"
#include "Render3D/Cluster/HIKARI_ClusteredRenderMode.h"
#include "Scene/HIKARI_SceneRenderCacheSync.h"
#include "Scene/HIKARI_ISystem.h"

namespace HIKARI {

    class AssetRegistry;
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
        int staticCachedShadowSkipCount = 0;
        int staticCachedBypassOldModelRendererCount = 0;
        int staticCachedFallbackCount = 0;
        int staticCachedCandidateCount = 0;
        int staticCachedCulledRecordCount = 0;
        int staticCachedSubmittedRecordCount = 0;
        int staticCachedSubmittedForwardRecordCount = 0;
        int staticCachedSubmittedShadowRecordCount = 0;
        bool frustumCullingEnabled = false;
    };

    struct RenderSubmissionOptions {
        bool useStaticDrawRecordCache = false;
        bool useCachedStaticForward = true;
        bool useCachedStaticShadow = true;
        bool skipOldStaticForwardWhenCached = true;
        bool skipOldStaticShadowWhenCached = true;
        bool bypassOldStaticModelRendererWhenFullyCached = true;
    };

    class RenderSubmissionSystem final : public ISystem {
    public:
        struct ClusteredCpuPreviewTarget {
            RENDER3D::CLUSTER::ClusteredRenderMode mode = RENDER3D::CLUSTER::ClusteredRenderMode::Off;
            const AssetRegistry* assetRegistry = nullptr;
            std::filesystem::path projectRoot{};
            uint64_t selectedObjectId = 0;
            RENDER3D::CLUSTER::ClusterDebugOptions debugOptions{};
        };

        std::string_view GetName() const override { return "RenderSubmissionSystem"; }

        void PreRender(World& world, const FrameContext& frame) override;

        static void SetActiveRenderCamera(const Camera3D* camera);
        static void SetClusteredCpuPreviewTarget(
            RENDER3D::CLUSTER::ClusteredRenderMode mode,
            const AssetRegistry* assetRegistry,
            std::filesystem::path projectRoot,
            uint64_t selectedObjectId,
            RENDER3D::CLUSTER::ClusterDebugOptions debugOptions);
        static void ClearClusteredCpuPreviewTarget();
        static const RenderSubmissionDebugStats& GetDebugStats();
        static void SetUseStaticDrawRecordCache(bool enabled);
        static bool IsUseStaticDrawRecordCacheEnabled();
        static void SetUseCachedStaticForward(bool enabled);
        static bool IsUseCachedStaticForwardEnabled();
        static void SetUseCachedStaticShadow(bool enabled);
        static bool IsUseCachedStaticShadowEnabled();
        static void SetBypassOldStaticModelRenderer(bool enabled);
        static bool IsBypassOldStaticModelRendererEnabled();
        static const RENDER3D::RUNTIME::SceneRenderCache& GetSceneRenderCache();
        static const RENDER3D::RUNTIME::SceneRenderCache::Stats& GetSceneRenderCacheStats();
        static const RENDER3D::RUNTIME::SurfaceDrawPacketBuilder& GetSurfaceDrawPacketBuilder();
        static const RENDER3D::RUNTIME::SurfaceDrawPacketBuilder::Stats& GetSurfaceDrawPacketStats();
        static const RENDER3D::RUNTIME::StaticDrawRecordCache& GetStaticDrawRecordCache();
        static const RENDER3D::RUNTIME::StaticDrawRecordCache::Stats& GetStaticDrawRecordCacheStats();
        static const RENDER3D::RUNTIME::StaticDrawRecordSubmitStats& GetStaticDrawRecordSubmitStats();

    private:
        static RenderSubmissionDebugStats sDebugStats_;
        static RenderSubmissionOptions sOptions_;
        static const Camera3D* sActiveRenderCamera_;
        static RENDER3D::RUNTIME::SceneRenderCache sSceneRenderCache_;
        static RENDER3D::RUNTIME::SurfaceDrawPacketBuilder sSurfaceDrawPacketBuilder_;
        static RENDER3D::RUNTIME::StaticDrawRecordCache sStaticDrawRecordCache_;
        static RENDER3D::RUNTIME::StaticRecordSubmitOptions sStaticRecordSubmitOptions_;
        static RENDER3D::RUNTIME::StaticDrawRecordSubmitter sStaticDrawRecordSubmitter_;
        static RENDER3D::RUNTIME::StaticDrawRecordSubmitStats sStaticDrawRecordSubmitStats_;
        static SceneRenderCacheSync sSceneRenderCacheSync_;

        static ClusteredCpuPreviewTarget sClusteredCpuPreviewTarget_;
    };

} // namespace HIKARI
