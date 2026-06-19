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

    struct GpuSceneStaticBatchStats {
        uint32_t recordCount = 0;
        uint32_t resourceBackedRecordCount = 0;
        uint32_t missingResourceHandleRecordCount = 0;
        uint32_t reorderedRecordCount = 0;
        uint32_t rawPsoRunCount = 0;
        uint32_t sortedPsoRunCount = 0;
        uint32_t rawMaterialRunCount = 0;
        uint32_t sortedMaterialRunCount = 0;
        uint32_t rawTextureSetRunCount = 0;
        uint32_t sortedTextureSetRunCount = 0;
        uint32_t rawGeometryRunCount = 0;
        uint32_t sortedGeometryRunCount = 0;
        uint32_t rawMeshResourceRunCount = 0;
        uint32_t sortedMeshResourceRunCount = 0;
        uint32_t rawMaterialResourceRunCount = 0;
        uint32_t sortedMaterialResourceRunCount = 0;
        uint32_t rawClusterResourceRunCount = 0;
        uint32_t sortedClusterResourceRunCount = 0;
        bool sortApplied = false;
    };

    struct GpuSceneRegistryStats {
        uint32_t sourceRecordCount = 0;
        uint32_t forwardRoutedRecordCount = 0;
        uint32_t forwardOpaqueResidentRecordCount = 0;
        uint32_t forwardOpaqueClusterCandidateRecordCount = 0;
        uint32_t unsupportedForwardRecordCount = 0;
        bool strictGpuDrivenMainline = false;
        uint32_t cpuPlannerBuildSuppressedCount = 0;
        uint32_t cpuPlannerRecordSuppressedCount = 0;
        uint32_t blockedForwardDepthAwareRecordCount = 0;
        uint32_t blockedForwardTransparentRecordCount = 0;
        uint32_t blockedShadowRecordCount = 0;
        uint32_t forwardSkinnedTraditionalRecordCount = 0;
        uint32_t shadowSkinnedTraditionalRecordCount = 0;

        RUNTIME::SurfaceGpuSceneBuildStats forwardOpaqueGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardDepthAwareGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardTransparentGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardOpaqueSkinnedTraditionalGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardDepthAwareSkinnedTraditionalGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardTransparentSkinnedTraditionalGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats shadowSkinnedTraditionalGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardOpaqueTraditionalGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardDepthAwareTraditionalGpuSceneStats{};
        RUNTIME::SurfaceGpuSceneBuildStats forwardTransparentTraditionalGpuSceneStats{};
        GpuSceneStaticBatchStats forwardOpaqueBatchStats{};
        GpuSceneStaticBatchStats forwardDepthAwareBatchStats{};
        GpuSceneStaticBatchStats forwardTransparentBatchStats{};
        GpuSceneStaticBatchStats shadowBatchStats{};
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
        bool HasShadowPassSource() const;
        const std::vector<GpuSceneSurfaceRecord>& GetSurfaceRecords() const;
        const GpuSceneRegistryStats& GetStats() const;

    public:
        struct TraditionalSkinnedStream {
            std::vector<RUNTIME::SurfaceDrawPacket> packets{};
            std::vector<uint32_t> executablePacketIndices{};
            std::vector<RUNTIME::SurfaceDrawCommand> commands{};
            std::vector<RUNTIME::SurfaceGpuSceneInstance> instances{};
            std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> materialSources{};
            std::vector<std::vector<MATH::Mat4>> jointPalettes{};

            void Clear();
            bool HasCommands() const;
        };

    private:
        struct ObjectCoverage {
            uint32_t expectedForwardRecordCount = 0;
            uint32_t handledForwardRecordCount = 0;
            std::unordered_set<uint64_t> forwardBypassSurfaceKeys{};
        };

        void RecordForwardExpectedSurface(const GpuSceneSurfaceRecord& record);
        void RecordForwardHandledSurface(const GpuSceneSurfaceRecord& record);
        void RebuildForwardFromSceneCache(const GpuSceneRegistrySyncInput& input);
        void SuppressForwardCpuPlannerViews(
            const GpuSceneRegistrySyncInput& input);
        bool TryPatchForwardDataFromSceneCache(const GpuSceneRegistrySyncInput& input);
        void RebuildForwardSceneSource();
        void ClearFrameDirtyRanges();

        std::vector<GpuSceneSurfaceRecord> surfaceRecords_{};
        std::vector<uint32_t> forwardOpaqueResidentRecordIndices_{};
        std::vector<uint32_t> forwardDepthAwareResidentRecordIndices_{};
        std::vector<uint32_t> forwardTransparentResidentRecordIndices_{};
        std::vector<uint32_t> shadowResidentRecordIndices_{};
        std::vector<uint32_t> forwardOpaqueSkinnedRecordIndices_{};
        std::vector<uint32_t> forwardDepthAwareSkinnedRecordIndices_{};
        std::vector<uint32_t> forwardTransparentSkinnedRecordIndices_{};
        std::vector<uint32_t> shadowSkinnedRecordIndices_{};
        std::vector<uint32_t> forwardOpaqueGpuSceneIndexByRecord_{};
        std::vector<uint32_t> forwardDepthAwareGpuSceneIndexByRecord_{};
        std::vector<uint32_t> forwardTransparentGpuSceneIndexByRecord_{};
        std::vector<uint32_t> shadowGpuSceneIndexByRecord_{};
        std::vector<RUNTIME::SurfaceGpuSceneInstance> forwardOpaqueGpuSceneInstances_{};
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> forwardOpaqueMaterialSources_{};
        std::vector<RUNTIME::SurfaceGpuSceneInstance> forwardDepthAwareGpuSceneInstances_{};
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> forwardDepthAwareMaterialSources_{};
        std::vector<RUNTIME::SurfaceGpuSceneInstance> forwardTransparentGpuSceneInstances_{};
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> forwardTransparentMaterialSources_{};
        std::vector<RUNTIME::SurfaceGpuSceneInstance> shadowGpuSceneInstances_{};
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> shadowMaterialSources_{};
        TraditionalSkinnedStream forwardOpaqueSkinnedStream_{};
        TraditionalSkinnedStream forwardDepthAwareSkinnedStream_{};
        TraditionalSkinnedStream forwardTransparentSkinnedStream_{};
        TraditionalSkinnedStream shadowSkinnedStream_{};
        std::unordered_map<uint64_t, ObjectCoverage> objectCoverage_{};
        GpuDrivenSceneSource sceneSource_{};
        GpuSceneRegistryStats stats_{};
        uint64_t layoutVersion_ = 0;
        uint64_t routingVersion_ = 0;
        uint64_t dataVersion_ = 0;
        uint32_t sourceSurfaceCount_ = 0;
    };

} // namespace HIKARI::RENDER3D::GPUDRIVEN
