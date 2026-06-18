#include "Render3D/GpuDriven/HIKARI_GpuSceneRegistry.h"

#include <algorithm>
#include <limits>

#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI::RENDER3D::GPUDRIVEN {

    namespace {
        uint32_t ClampToUint32(size_t value) {
            return static_cast<uint32_t>(
                (std::min)(value, static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
        }

        void AccumulateSurfaceGpuSceneStats(
            RUNTIME::SurfaceGpuSceneBuildStats& dst,
            const RUNTIME::SurfaceGpuSceneBuildStats& src) {

            dst.commandCount += src.commandCount;
            dst.instanceCount += src.instanceCount;
            dst.skippedInvalidCommandCount += src.skippedInvalidCommandCount;
            dst.skippedInvalidPacketCount += src.skippedInvalidPacketCount;
            dst.maxCommandInstanceCount =
                (std::max)(dst.maxCommandInstanceCount, src.maxCommandInstanceCount);
            dst.resourceBackedInstanceCount += src.resourceBackedInstanceCount;
            dst.missingResourceHandleInstanceCount += src.missingResourceHandleInstanceCount;
            dst.clusterResourceInstanceCount += src.clusterResourceInstanceCount;
            dst.clusterShaderVisibleInstanceCount += src.clusterShaderVisibleInstanceCount;
            dst.clusterSurfaceRangeInstanceCount += src.clusterSurfaceRangeInstanceCount;
            dst.clusterMissingSurfaceRangeInstanceCount += src.clusterMissingSurfaceRangeInstanceCount;
        }

        void ResetPassSource(
            GpuDrivenPassSource& pass,
            const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances,
            const std::vector<RUNTIME::SurfaceGpuSceneMaterialSource>* materialSources,
            uint32_t gpuSceneBaseIndex,
            GpuDrivenBackendKind backend,
            bool clusterEligible) {

            pass.traditionalIndirect.Reset();
            pass.instances = instances;
            pass.materialSources = materialSources;
            pass.gpuSceneBaseIndex = gpuSceneBaseIndex;
            pass.gpuSceneInstanceCount =
                instances != nullptr
                    ? ClampToUint32(instances->size())
                    : 0u;
            pass.preferredBackend = backend;
            pass.clusterEligible = clusterEligible;
            pass.dirtyRanges.clear();
        }

        void ResetTraditionalIndirectView(
            GpuDrivenPassSource& pass,
            const std::vector<RUNTIME::SurfaceDrawPacket>* packets,
            const std::vector<uint32_t>* executablePacketIndices,
            const std::vector<RUNTIME::SurfaceDrawCommand>* commands,
            const std::vector<RUNTIME::SurfaceGpuSceneInstance>* instances,
            const std::vector<RUNTIME::SurfaceGpuSceneMaterialSource>* materialSources,
            uint32_t gpuSceneBaseIndex) {

            pass.traditionalIndirect.packets = packets;
            pass.traditionalIndirect.executablePacketIndices =
                executablePacketIndices;
            pass.traditionalIndirect.commands = commands;
            pass.traditionalIndirect.instances = instances;
            pass.traditionalIndirect.materialSources = materialSources;
            pass.traditionalIndirect.gpuSceneBaseIndex = gpuSceneBaseIndex;
            pass.traditionalIndirect.gpuSceneInstanceCount =
                instances != nullptr
                    ? ClampToUint32(instances->size())
                    : 0u;
        }

        RUNTIME::SurfaceGpuSceneMaterialSource BuildTraditionalMaterialSource(
            const RUNTIME::SurfaceDrawPacket& packet,
            const RUNTIME::SurfaceGpuSceneInstance& instance) {

            RUNTIME::SurfaceGpuSceneMaterialSource source{};
            source.model = packet.model;
            source.materialOverride = packet.materialOverride;
            source.materialIndex = packet.materialIndex;
            source.materialKey = packet.key.materialKey;
            source.world = instance.world;
            source.normalMatrix = instance.normalMatrix;
            source.receiveShadow = packet.receiveShadow;
            source.fxFlags = instance.fxFlags;
            for (size_t i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                source.fxUser[i] = instance.fxUser[i];
            }
            return source;
        }

        void BuildTraditionalMaterialSources(
            const std::vector<RUNTIME::SurfaceDrawPacket>& packets,
            const std::vector<RUNTIME::SurfaceGpuSceneInstance>& instances,
            std::vector<RUNTIME::SurfaceGpuSceneMaterialSource>& materialSources) {

            materialSources.clear();
            materialSources.reserve(instances.size());
            for (const RUNTIME::SurfaceGpuSceneInstance& instance : instances) {
                if (instance.sourcePacketIndex >= packets.size()) {
                    materialSources.push_back({});
                    continue;
                }
                materialSources.push_back(
                    BuildTraditionalMaterialSource(
                        packets[instance.sourcePacketIndex],
                        instance));
            }
        }

        uint64_t HashAppend(uint64_t seed, uint64_t value) {
            constexpr uint64_t kMul = 1099511628211ull;
            seed ^= value;
            seed *= kMul;
            return seed;
        }

        uint64_t BuildSourceLayoutVersion(uint64_t layoutVersion, uint64_t routingVersion) {
            uint64_t value = 1469598103934665603ull;
            value = HashAppend(value, layoutVersion);
            value = HashAppend(value, routingVersion);
            return value;
        }

        void AppendDirtyRange(
            std::vector<GpuSceneDirtyRange>& ranges,
            uint32_t firstInstance,
            uint32_t instanceCount) {

            if (instanceCount == 0u) {
                return;
            }
            if (!ranges.empty()) {
                GpuSceneDirtyRange& last = ranges.back();
                if (last.firstInstance + last.instanceCount == firstInstance) {
                    last.instanceCount += instanceCount;
                    return;
                }
            }

            ranges.push_back({ firstInstance, instanceCount });
        }
    }

    void GpuSceneRegistry::Clear() {
        surfaceRecords_.clear();
        forwardOpaqueResidentRecordIndices_.clear();
        forwardOpaqueGpuSceneIndexByRecord_.clear();
        forwardOpaqueGpuSceneInstances_.clear();
        forwardOpaqueMaterialSources_.clear();
        forwardDepthAwareGpuSceneInstances_.clear();
        forwardDepthAwareMaterialSources_.clear();
        forwardTransparentGpuSceneInstances_.clear();
        forwardTransparentMaterialSources_.clear();
        forwardOpaqueTraditionalGpuSceneInstances_.clear();
        forwardOpaqueTraditionalMaterialSources_.clear();
        forwardDepthAwareTraditionalGpuSceneInstances_.clear();
        forwardDepthAwareTraditionalMaterialSources_.clear();
        forwardTransparentTraditionalGpuSceneInstances_.clear();
        forwardTransparentTraditionalMaterialSources_.clear();
        shadowTraditionalMaterialSources_.clear();
        surfacePacketBuilder_.Clear();
        surfacePacketPlanner_ = {};
        objectCoverage_.clear();
        sceneSource_.Reset();
        stats_ = {};
        layoutVersion_ = 0;
        routingVersion_ = 0;
        dataVersion_ = 0;
        sourceSurfaceCount_ = 0;
    }

    void GpuSceneRegistry::SyncForwardFromSceneCache(
        const GpuSceneRegistrySyncInput& input) {

        if (input.sceneCache == nullptr) {
            Clear();
            return;
        }

        const uint64_t layoutVersion = input.sceneCache->GetSurfaceVersion();
        const uint64_t routingVersion = input.sceneCache->GetSurfaceRoutingVersion();
        const uint64_t dataVersion = input.sceneCache->GetSurfaceDataVersion();
        const uint32_t surfaceCount =
            ClampToUint32(input.sceneCache->GetSurfaceInstances().size());

        const bool layoutChanged =
            layoutVersion_ != layoutVersion ||
            routingVersion_ != routingVersion ||
            sourceSurfaceCount_ != surfaceCount;
        if (layoutChanged || !sceneSource_.HasAnyGpuSceneRanges()) {
            RebuildForwardFromSceneCache(input);
            return;
        }

        ClearFrameDirtyRanges();
        if (dataVersion_ == dataVersion) {
            return;
        }

        if (!TryPatchForwardDataFromSceneCache(input)) {
            RebuildForwardFromSceneCache(input);
            return;
        }

        dataVersion_ = dataVersion;
        sceneSource_.layoutVersion =
            BuildSourceLayoutVersion(layoutVersion_, routingVersion_);
        sceneSource_.sourceVersion = dataVersion_;
    }

    void GpuSceneRegistry::RebuildForwardFromSceneCache(
        const GpuSceneRegistrySyncInput& input) {

        Clear();
        if (input.sceneCache == nullptr) {
            return;
        }

        const std::vector<RUNTIME::SceneSurfaceInstance>& surfaces =
            input.sceneCache->GetSurfaceInstances();
        surfaceRecords_.reserve(surfaces.size());
        for (uint32_t surfaceIndex = 0;
            surfaceIndex < surfaces.size();
            ++surfaceIndex) {

            GpuSceneSurfaceRecord record =
                BuildGpuSceneSurfaceRecord(
                    surfaces[surfaceIndex],
                    surfaceIndex);
            if (record.forwardCandidate) {
                RecordForwardExpectedSurface(record);
            }
            surfaceRecords_.push_back(std::move(record));
        }

        stats_.sourceRecordCount = ClampToUint32(surfaceRecords_.size());

        std::vector<uint32_t> routedRecordIndices{};
        routedRecordIndices.reserve(surfaceRecords_.size());
        for (uint32_t recordIndex = 0;
            recordIndex < surfaceRecords_.size();
            ++recordIndex) {

            const GpuSceneSurfaceRecord& record = surfaceRecords_[recordIndex];
            if (record.valid && record.forwardCandidate && record.key.resourceKeyValid) {
                routedRecordIndices.push_back(recordIndex);
            }
        }
        stats_.forwardRoutedRecordCount = ClampToUint32(routedRecordIndices.size());

        forwardOpaqueResidentRecordIndices_.reserve(routedRecordIndices.size());
        for (const uint32_t recordIndex : routedRecordIndices) {
            if (recordIndex >= surfaceRecords_.size()) {
                continue;
            }

            const GpuSceneSurfaceRecord& record = surfaceRecords_[recordIndex];
            if (IsGpuSceneForwardOpaqueResidentRecord(record)) {
                forwardOpaqueResidentRecordIndices_.push_back(recordIndex);
                ++stats_.forwardOpaqueClusterCandidateRecordCount;
                RecordForwardHandledSurface(record);
            } else if (record.forwardCandidate) {
                ++stats_.unsupportedForwardRecordCount;
            }
        }
        stats_.forwardOpaqueResidentRecordCount =
            ClampToUint32(forwardOpaqueResidentRecordIndices_.size());

        forwardOpaqueGpuSceneIndexByRecord_.assign(
            surfaceRecords_.size(),
            RUNTIME::kInvalidRenderSurfaceIndex);
        RUNTIME::SurfaceGpuSceneBuildStats opaqueStats{};
        AccumulateSurfaceGpuSceneStats(
            opaqueStats,
            BuildGpuSceneInstanceList(
                surfaceRecords_,
                forwardOpaqueResidentRecordIndices_,
                forwardOpaqueGpuSceneInstances_,
                forwardOpaqueMaterialSources_));
        for (uint32_t localInstanceIndex = 0;
            localInstanceIndex < forwardOpaqueResidentRecordIndices_.size();
            ++localInstanceIndex) {

            const uint32_t recordIndex =
                forwardOpaqueResidentRecordIndices_[localInstanceIndex];
            if (recordIndex < forwardOpaqueGpuSceneIndexByRecord_.size()) {
                forwardOpaqueGpuSceneIndexByRecord_[recordIndex] = localInstanceIndex;
            }
        }
        stats_.forwardOpaqueGpuSceneStats = opaqueStats;

        layoutVersion_ = input.sceneCache->GetSurfaceVersion();
        routingVersion_ = input.sceneCache->GetSurfaceRoutingVersion();
        dataVersion_ = input.sceneCache->GetSurfaceDataVersion();
        sourceSurfaceCount_ = ClampToUint32(surfaces.size());
        RebuildForwardTraditionalIndirectViews(input);
        RebuildForwardSceneSource();
    }

    void GpuSceneRegistry::RebuildForwardTraditionalIndirectViews(
        const GpuSceneRegistrySyncInput& input) {

        forwardOpaqueTraditionalGpuSceneInstances_.clear();
        forwardOpaqueTraditionalMaterialSources_.clear();
        forwardDepthAwareTraditionalGpuSceneInstances_.clear();
        forwardDepthAwareTraditionalMaterialSources_.clear();
        forwardTransparentTraditionalGpuSceneInstances_.clear();
        forwardTransparentTraditionalMaterialSources_.clear();
        shadowTraditionalMaterialSources_.clear();
        surfacePacketBuilder_.Clear();
        surfacePacketPlanner_ = {};
        stats_.surfacePacketStats = {};
        stats_.surfacePacketPlanStats = {};
        stats_.forwardOpaqueTraditionalGpuSceneStats = {};
        stats_.forwardDepthAwareTraditionalGpuSceneStats = {};
        stats_.forwardTransparentTraditionalGpuSceneStats = {};

        if (input.sceneCache == nullptr) {
            return;
        }

        surfacePacketBuilder_.BuildFromSceneRenderCache(*input.sceneCache);
        RUNTIME::SurfaceDrawPacketPlanOptions options{};
        options.buildForwardPlan = true;
        options.bypassLegacyForward = true;
        options.buildShadowPlan = true;
        options.bypassLegacyShadow = true;
        options.enableCpuFrustumCulling = false;
        surfacePacketPlanner_.Build(
            surfacePacketBuilder_,
            options,
            stats_.surfacePacketPlanStats);
        stats_.surfacePacketStats = surfacePacketBuilder_.GetStats();

        const std::vector<RUNTIME::SurfaceDrawPacket>& packets =
            surfacePacketBuilder_.GetPackets();

        std::vector<RUNTIME::SurfaceDrawCommand>& opaqueCommands =
            surfacePacketPlanner_.GetExecutableForwardOpaqueCommands();
        stats_.forwardOpaqueTraditionalGpuSceneStats =
            RUNTIME::SurfaceGpuSceneWriter::BuildCommandRanges(
                packets,
                surfacePacketPlanner_.GetExecutableForwardOpaquePacketIndices(),
                opaqueCommands,
                forwardOpaqueTraditionalGpuSceneInstances_);
        BuildTraditionalMaterialSources(
            packets,
            forwardOpaqueTraditionalGpuSceneInstances_,
            forwardOpaqueTraditionalMaterialSources_);

        std::vector<RUNTIME::SurfaceDrawCommand>& depthAwareCommands =
            surfacePacketPlanner_.GetExecutableForwardDepthAwareCommands();
        stats_.forwardDepthAwareTraditionalGpuSceneStats =
            RUNTIME::SurfaceGpuSceneWriter::BuildCommandRanges(
                packets,
                surfacePacketPlanner_.GetExecutableForwardDepthAwarePacketIndices(),
                depthAwareCommands,
                forwardDepthAwareTraditionalGpuSceneInstances_);
        BuildTraditionalMaterialSources(
            packets,
            forwardDepthAwareTraditionalGpuSceneInstances_,
            forwardDepthAwareTraditionalMaterialSources_);

        std::vector<RUNTIME::SurfaceDrawCommand>& transparentCommands =
            surfacePacketPlanner_.GetExecutableForwardTransparentCommands();
        stats_.forwardTransparentTraditionalGpuSceneStats =
            RUNTIME::SurfaceGpuSceneWriter::BuildCommandRanges(
                packets,
                surfacePacketPlanner_.GetExecutableForwardTransparentPacketIndices(),
                transparentCommands,
                forwardTransparentTraditionalGpuSceneInstances_);
        BuildTraditionalMaterialSources(
            packets,
            forwardTransparentTraditionalGpuSceneInstances_,
            forwardTransparentTraditionalMaterialSources_);

        BuildTraditionalMaterialSources(
            packets,
            surfacePacketPlanner_.GetShadowGpuSceneInstances(),
            shadowTraditionalMaterialSources_);
    }

    bool GpuSceneRegistry::TryPatchForwardDataFromSceneCache(
        const GpuSceneRegistrySyncInput& input) {

        if (input.sceneCache == nullptr) {
            return false;
        }
        if (!forwardOpaqueTraditionalGpuSceneInstances_.empty() ||
            !forwardDepthAwareTraditionalGpuSceneInstances_.empty() ||
            !forwardTransparentTraditionalGpuSceneInstances_.empty()) {
            return false;
        }
        const std::vector<RUNTIME::SceneSurfaceInstance>& surfaces =
            input.sceneCache->GetSurfaceInstances();
        if (surfaces.size() != surfaceRecords_.size()) {
            return false;
        }

        std::vector<uint32_t> dirtySurfaceIndices =
            input.sceneCache->GetDirtySurfaceIndices();
        std::sort(dirtySurfaceIndices.begin(), dirtySurfaceIndices.end());
        dirtySurfaceIndices.erase(
            std::unique(dirtySurfaceIndices.begin(), dirtySurfaceIndices.end()),
            dirtySurfaceIndices.end());

        std::vector<uint32_t> singleRecordIndex{};
        singleRecordIndex.reserve(1);
        std::vector<RUNTIME::SurfaceGpuSceneInstance> singleInstance{};
        std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> singleMaterial{};

        GpuDrivenPassSource& forwardOpaqueSource =
            sceneSource_.GetPass(GpuDrivenPassKind::ForwardOpaque);
        for (const uint32_t surfaceIndex : dirtySurfaceIndices) {
            if (surfaceIndex >= surfaces.size() ||
                surfaceIndex >= surfaceRecords_.size()) {
                return false;
            }

            GpuSceneSurfaceRecord newRecord =
                BuildGpuSceneSurfaceRecord(surfaces[surfaceIndex], surfaceIndex);
            const GpuSceneSurfaceRecord& oldRecord = surfaceRecords_[surfaceIndex];
            const bool oldOpaque = IsGpuSceneForwardOpaqueResidentRecord(oldRecord);
            const bool newOpaque = IsGpuSceneForwardOpaqueResidentRecord(newRecord);
            if (oldOpaque != newOpaque ||
                oldRecord.key.sortKey != newRecord.key.sortKey ||
                oldRecord.key.psoKey != newRecord.key.psoKey ||
                oldRecord.key.geometryKey != newRecord.key.geometryKey ||
                oldRecord.key.materialKey != newRecord.key.materialKey) {
                return false;
            }

            surfaceRecords_[surfaceIndex] = std::move(newRecord);
            if (!newOpaque) {
                continue;
            }
            if (surfaceIndex >= forwardOpaqueGpuSceneIndexByRecord_.size()) {
                return false;
            }
            const uint32_t gpuSceneInstanceIndex =
                forwardOpaqueGpuSceneIndexByRecord_[surfaceIndex];
            if (gpuSceneInstanceIndex == RUNTIME::kInvalidRenderSurfaceIndex ||
                gpuSceneInstanceIndex >= forwardOpaqueGpuSceneInstances_.size() ||
                gpuSceneInstanceIndex >= forwardOpaqueMaterialSources_.size()) {
                return false;
            }

            singleRecordIndex.clear();
            singleRecordIndex.push_back(surfaceIndex);
            const RUNTIME::SurfaceGpuSceneBuildStats buildStats =
                BuildGpuSceneInstanceList(
                    surfaceRecords_,
                    singleRecordIndex,
                    singleInstance,
                    singleMaterial);
            if (buildStats.instanceCount != 1u ||
                singleInstance.size() != 1u ||
                singleMaterial.size() != 1u) {
                return false;
            }

            forwardOpaqueGpuSceneInstances_[gpuSceneInstanceIndex] = singleInstance[0];
            forwardOpaqueMaterialSources_[gpuSceneInstanceIndex] = singleMaterial[0];
            AppendDirtyRange(
                forwardOpaqueSource.dirtyRanges,
                gpuSceneInstanceIndex,
                1u);
        }

        return true;
    }

    void GpuSceneRegistry::RebuildForwardSceneSource() {
        uint32_t cursor = 0;

        ResetPassSource(
            sceneSource_.GetPass(GpuDrivenPassKind::ForwardOpaque),
            &forwardOpaqueGpuSceneInstances_,
            &forwardOpaqueMaterialSources_,
            cursor,
            GpuDrivenBackendKind::MeshShader,
            true);
        cursor += ClampToUint32(forwardOpaqueGpuSceneInstances_.size());

        ResetTraditionalIndirectView(
            sceneSource_.GetPass(GpuDrivenPassKind::ForwardOpaque),
            &surfacePacketBuilder_.GetPackets(),
            &surfacePacketPlanner_.GetExecutableForwardOpaquePacketIndices(),
            &surfacePacketPlanner_.GetExecutableForwardOpaqueCommands(),
            &forwardOpaqueTraditionalGpuSceneInstances_,
            &forwardOpaqueTraditionalMaterialSources_,
            cursor);
        cursor += ClampToUint32(forwardOpaqueTraditionalGpuSceneInstances_.size());

        ResetPassSource(
            sceneSource_.GetPass(GpuDrivenPassKind::ForwardDepthAware),
            &forwardDepthAwareGpuSceneInstances_,
            &forwardDepthAwareMaterialSources_,
            cursor,
            GpuDrivenBackendKind::TraditionalIndirect,
            true);
        cursor += ClampToUint32(forwardDepthAwareGpuSceneInstances_.size());

        ResetTraditionalIndirectView(
            sceneSource_.GetPass(GpuDrivenPassKind::ForwardDepthAware),
            &surfacePacketBuilder_.GetPackets(),
            &surfacePacketPlanner_.GetExecutableForwardDepthAwarePacketIndices(),
            &surfacePacketPlanner_.GetExecutableForwardDepthAwareCommands(),
            &forwardDepthAwareTraditionalGpuSceneInstances_,
            &forwardDepthAwareTraditionalMaterialSources_,
            cursor);
        cursor += ClampToUint32(forwardDepthAwareTraditionalGpuSceneInstances_.size());

        ResetPassSource(
            sceneSource_.GetPass(GpuDrivenPassKind::ForwardTransparent),
            &forwardTransparentGpuSceneInstances_,
            &forwardTransparentMaterialSources_,
            cursor,
            GpuDrivenBackendKind::TraditionalIndirect,
            true);
        cursor += ClampToUint32(forwardTransparentGpuSceneInstances_.size());

        ResetTraditionalIndirectView(
            sceneSource_.GetPass(GpuDrivenPassKind::ForwardTransparent),
            &surfacePacketBuilder_.GetPackets(),
            &surfacePacketPlanner_.GetExecutableForwardTransparentPacketIndices(),
            &surfacePacketPlanner_.GetExecutableForwardTransparentCommands(),
            &forwardTransparentTraditionalGpuSceneInstances_,
            &forwardTransparentTraditionalMaterialSources_,
            cursor);
        cursor += ClampToUint32(forwardTransparentTraditionalGpuSceneInstances_.size());

        ResetPassSource(
            sceneSource_.GetPass(GpuDrivenPassKind::Shadow),
            nullptr,
            nullptr,
            cursor,
            GpuDrivenBackendKind::TraditionalIndirect,
            false);

        ResetTraditionalIndirectView(
            sceneSource_.GetPass(GpuDrivenPassKind::Shadow),
            &surfacePacketBuilder_.GetPackets(),
            &surfacePacketPlanner_.GetExecutableShadowPacketIndices(),
            &surfacePacketPlanner_.GetExecutableShadowCommands(),
            &surfacePacketPlanner_.GetShadowGpuSceneInstances(),
            &shadowTraditionalMaterialSources_,
            cursor);
        cursor += ClampToUint32(
            surfacePacketPlanner_.GetShadowGpuSceneInstances().size());

        sceneSource_.layoutVersion =
            BuildSourceLayoutVersion(layoutVersion_, routingVersion_);
        sceneSource_.sourceVersion = dataVersion_;
        sceneSource_.sourceInstanceCount = cursor;
    }

    void GpuSceneRegistry::ClearFrameDirtyRanges() {
        for (GpuDrivenPassSource& pass : sceneSource_.passes) {
            pass.dirtyRanges.clear();
        }
    }

    bool GpuSceneRegistry::HasForwardCoverageForObject(
        RUNTIME::SceneRenderObjectId objectId) const {

        const auto found = objectCoverage_.find(objectId.value);
        return
            found != objectCoverage_.end() &&
            found->second.handledForwardRecordCount > 0;
    }

    bool GpuSceneRegistry::HasFullForwardCoverageForObject(
        RUNTIME::SceneRenderObjectId objectId) const {

        const auto found = objectCoverage_.find(objectId.value);
        return
            found != objectCoverage_.end() &&
            found->second.expectedForwardRecordCount > 0 &&
            found->second.expectedForwardRecordCount ==
            found->second.handledForwardRecordCount;
    }

    bool GpuSceneRegistry::ShouldBypassLegacyForwardSurface(
        RUNTIME::SceneRenderObjectId objectId,
        uint32_t nodeIndex,
        uint32_t meshIndex,
        uint32_t primitiveIndex) const {

        const auto found = objectCoverage_.find(objectId.value);
        if (found == objectCoverage_.end()) {
            return false;
        }
        return
            found->second.forwardBypassSurfaceKeys.find(
                BuildGpuSceneSurfaceFilterKey(nodeIndex, meshIndex, primitiveIndex)) !=
            found->second.forwardBypassSurfaceKeys.end();
    }

    const GpuDrivenSceneSource& GpuSceneRegistry::GetSceneSource() const {
        return sceneSource_;
    }

    bool GpuSceneRegistry::HasShadowPassSource() const {
        const GpuDrivenPassSource& shadow =
            sceneSource_.GetPass(GpuDrivenPassKind::Shadow);
        return
            shadow.traditionalIndirect.HasCommands() &&
            shadow.traditionalIndirect.HasGpuSceneInstances();
    }

    const std::vector<GpuSceneSurfaceRecord>& GpuSceneRegistry::GetSurfaceRecords() const {
        return surfaceRecords_;
    }

    const GpuSceneRegistryStats& GpuSceneRegistry::GetStats() const {
        return stats_;
    }

    void GpuSceneRegistry::RecordForwardExpectedSurface(
        const GpuSceneSurfaceRecord& record) {

        if (!record.objectId.IsValid()) {
            return;
        }
        ++objectCoverage_[record.objectId.value].expectedForwardRecordCount;
    }

    void GpuSceneRegistry::RecordForwardHandledSurface(
        const GpuSceneSurfaceRecord& record) {

        if (!record.objectId.IsValid()) {
            return;
        }
        ObjectCoverage& coverage = objectCoverage_[record.objectId.value];
        ++coverage.handledForwardRecordCount;
        coverage.forwardBypassSurfaceKeys.insert(BuildGpuSceneSurfaceFilterKey(record));
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
