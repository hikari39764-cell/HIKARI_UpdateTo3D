#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Render3D/GpuDriven/HIKARI_GpuDrivenSceneSource.h"
#include "Render3D/GpuDriven/HIKARI_GpuSceneSurfaceRecord.h"
#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"
#include "Render3D/Runtime/HIKARI_SurfaceDrawPacket.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    struct GpuSceneRegistrySyncInput {
        const RUNTIME::SceneRenderCache* sceneCache = nullptr;
    };

    struct GpuSceneRegistryStats {
        uint32_t sourceRecordCount = 0;
        uint32_t forwardRoutedRecordCount = 0;
        uint32_t forwardOpaqueResidentRecordCount = 0;
        uint32_t forwardOpaqueClusterCandidateRecordCount = 0;
        uint32_t unsupportedForwardRecordCount = 0;

        RUNTIME::SurfaceGpuSceneBuildStats forwardOpaqueGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardDepthAwareGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardTransparentGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardOpaqueTraditionalGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardDepthAwareTraditionalGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardTransparentTraditionalGpuSceneStats{};
        RUNTIME::SurfaceDrawPacketBuilder::Stats surfacePacketStats{};
        RUNTIME::SurfaceDrawPacketPlanStats surfacePacketPlanStats{};
    };

    class GpuSceneRegistry final {
    public:
        void Clear();
        void SyncForwardFromSceneCache(const GpuSceneRegistrySyncInput& input);

        bool HasForwardCoverageForObject(RUNTIME::SceneRenderObjectId objectId) const;
        bool HasFullForwardCoverageForObject(RUNTIME::SceneRenderObjectId objectId) const;
        bool ShouldBypassLegacyForwardSurface(
            RUNTIME::SceneRenderObjectId objectId,
            uint32_t nodeIndex,
            uint32_t meshIndex,
            uint32_t primitiveIndex) const;

        const GpuDrivenSceneSource& GetSceneSource() const;
        const std::vector<GpuSceneSurfaceRecord>& GetSurfaceRecords() const;
        const GpuSceneRegistryStats& GetStats() const;

    private:
        struct ObjectCoverage {
            uint32_t expectedForwardRecordCount = 0;
            uint32_t handledForwardRecordCount = 0;
            std::unordered_set<uint64_t> forwardBypassSurfaceKeys{};
        };

        void RecordForwardExpectedSurface(const GpuSceneSurfaceRecord& record);
        void RecordForwardHandledSurface(const GpuSceneSurfaceRecord& record);
        void RebuildForwardFromSceneCache(const GpuSceneRegistrySyncInput& input);
        void RebuildForwardTraditionalIndirectViews(
            const GpuSceneRegistrySyncInput& input);
        bool TryPatchForwardDataFromSceneCache(const GpuSceneRegistrySyncInput& input);
        void RebuildForwardSceneSource();
        void ClearFrameDirtyRanges();

        std::vector<GpuSceneSurfaceRecord> surfaceRecords_{};
        std::vector<uint32_t> forwardOpaqueResidentRecordIndices_{};
        std::vector<uint32_t> forwardOpaqueGpuSceneIndexByRecord_{};
        std::vector<RUNTIME::SurfaceGpuSceneInstance> forwardOpaqueGpuSceneInstances_{};
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> forwardOpaqueMaterialSources_{};
        std::vector<RUNTIME::SurfaceGpuSceneInstance> forwardDepthAwareGpuSceneInstances_{};
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> forwardDepthAwareMaterialSources_{};
        std::vector<RUNTIME::SurfaceGpuSceneInstance> forwardTransparentGpuSceneInstances_{};
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> forwardTransparentMaterialSources_{};
        std::vector<RUNTIME::SurfaceGpuSceneInstance> forwardOpaqueTraditionalGpuSceneInstances_{};
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> forwardOpaqueTraditionalMaterialSources_{};
        std::vector<RUNTIME::SurfaceGpuSceneInstance> forwardDepthAwareTraditionalGpuSceneInstances_{};
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> forwardDepthAwareTraditionalMaterialSources_{};
        std::vector<RUNTIME::SurfaceGpuSceneInstance> forwardTransparentTraditionalGpuSceneInstances_{};
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> forwardTransparentTraditionalMaterialSources_{};
        RUNTIME::SurfaceDrawPacketBuilder surfacePacketBuilder_{};
        RUNTIME::SurfaceDrawPacketPlanner surfacePacketPlanner_{};
        std::unordered_map<uint64_t, ObjectCoverage> objectCoverage_{};
        GpuDrivenSceneSource sceneSource_{};
        GpuSceneRegistryStats stats_{};
        uint64_t layoutVersion_ = 0;
        uint64_t routingVersion_ = 0;
        uint64_t dataVersion_ = 0;
        uint32_t sourceSurfaceCount_ = 0;
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
