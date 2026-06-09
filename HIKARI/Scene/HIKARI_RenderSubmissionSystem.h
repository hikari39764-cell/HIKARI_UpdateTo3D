#pragma once

#include <cstdint>
#include <filesystem>
#include <string_view>

#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
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
        int surfacePacketForwardSkipCount = 0;
        int surfacePacketShadowSkipCount = 0;
        int runtimeSpecialModelCount = 0;
        int runtimeSpecialForwardModelCount = 0;
        int runtimeSpecialShadowModelCount = 0;
        bool surfacePacketMainRouteActive = false;
        bool frustumCullingEnabled = false;
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
        static const RENDER3D::RUNTIME::SceneRenderCache& GetSceneRenderCache();
        static const RENDER3D::RUNTIME::SceneRenderCache::Stats& GetSceneRenderCacheStats();
        static const RENDER3D::RUNTIME::SurfaceDrawPacketBuilder& GetSurfaceDrawPacketBuilder();
        static const RENDER3D::RUNTIME::SurfaceDrawPacketBuilder::Stats& GetSurfaceDrawPacketStats();
        static const RENDER3D::RUNTIME::SurfaceDrawPacketPlanStats& GetSurfaceDrawPacketPlanStats();

    private:
        static RenderSubmissionDebugStats sDebugStats_;
        static const Camera3D* sActiveRenderCamera_;
        static RENDER3D::RUNTIME::SceneRenderCache sSceneRenderCache_;
        static RENDER3D::RUNTIME::SurfaceDrawPacketBuilder sSurfaceDrawPacketBuilder_;
        static RENDER3D::RUNTIME::SurfaceDrawPacketPlanner sSurfaceDrawPacketPlanner_;
        static RENDER3D::RUNTIME::SurfaceDrawPacketPlanOptions sSurfaceDrawPacketPlanOptions_;
        static RENDER3D::RUNTIME::SurfaceDrawPacketPlanStats sSurfaceDrawPacketPlanStats_;
        static SceneRenderCacheSync sSceneRenderCacheSync_;

        static ClusteredCpuPreviewTarget sClusteredCpuPreviewTarget_;
    };

} // namespace HIKARI
