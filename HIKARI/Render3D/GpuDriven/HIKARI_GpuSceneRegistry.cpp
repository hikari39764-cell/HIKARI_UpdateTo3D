#include "Render3D/GpuDriven/HIKARI_GpuSceneRegistry.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

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
            dst.skippedInvalidRecordCount += src.skippedInvalidRecordCount;
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

        float ComputeWorldBoundsRadius(const Bounds& bounds) {
            const float ex = (bounds.max.x - bounds.min.x) * 0.5f;
            const float ey = (bounds.max.y - bounds.min.y) * 0.5f;
            const float ez = (bounds.max.z - bounds.min.z) * 0.5f;
            return std::sqrt(ex * ex + ey * ey + ez * ez);
        }

        float ComputeWorldBoundsMainAreaProxy(const Bounds& bounds) {
            float sx = (std::max)(bounds.max.x - bounds.min.x, 0.0f);
            float sy = (std::max)(bounds.max.y - bounds.min.y, 0.0f);
            float sz = (std::max)(bounds.max.z - bounds.min.z, 0.0f);
            if (sx < sy) {
                std::swap(sx, sy);
            }
            if (sy < sz) {
                std::swap(sy, sz);
            }
            if (sx < sy) {
                std::swap(sx, sy);
            }
            return sx * sy;
        }

        float ComputeDepthOccluderScore(const Bounds& bounds, float radius) {
            const float mainArea = ComputeWorldBoundsMainAreaProxy(bounds);
            return std::sqrt((std::max)(mainArea, 0.0f)) + radius * 0.15f;
        }

        constexpr float kDepthVisibilityMinOccluderRadius = 0.35f;
        constexpr size_t kDepthVisibilityMaxOccluderRecords = 1024;
        constexpr float kShadowCasterMinRadius = 0.28f;
        constexpr float kShadowCasterMinMainArea = 0.06f;
        constexpr float kShadowAlphaMaskMinRadius = 1.10f;
        constexpr float kShadowAlphaMaskMinMainArea = 0.35f;
        constexpr float kShadowDoubleSidedMinRadius = 0.85f;
        constexpr float kShadowDoubleSidedMinMainArea = 0.20f;

        struct DepthPrepassOccluderCandidate {
            uint32_t recordIndex = RUNTIME::kInvalidRenderSurfaceIndex;
            float radius = 0.0f;
            float score = 0.0f;
        };

        bool HasDepthVisibilitySafeMaterial(const GpuSceneSurfaceRecord& record) {
            return
                !record.key.alphaMasked &&
                !record.key.transparent &&
                !record.key.depthAware &&
                !record.key.materialFx &&
                !record.key.waterMaterialFx;
        }

        bool IsDepthVisibilityOccluderRecord(
            const GpuSceneSurfaceRecord& record,
            bool strictSize) {

            if (!IsGpuSceneForwardOpaqueResidentRecord(record) ||
                !HasDepthVisibilitySafeMaterial(record)) {
                return false;
            }
            if (record.key.doubleSided) {
                return false;
            }
            if (!strictSize) {
                return true;
            }
            return ComputeWorldBoundsRadius(record.worldBounds) >=
                kDepthVisibilityMinOccluderRadius;
        }

        bool HasShadowCasterSafeMaterial(const GpuSceneSurfaceRecord& record) {
            return
                !record.key.transparent &&
                !record.key.depthAware &&
                !record.key.materialFx &&
                !record.key.waterMaterialFx;
        }

        bool IsGpuSceneStaticShadowCasterRecord(
            const GpuSceneSurfaceRecord& record) {

            if (!IsGpuSceneShadowResidentRecord(record) ||
                !HasShadowCasterSafeMaterial(record)) {
                return false;
            }

            const float radius = ComputeWorldBoundsRadius(record.worldBounds);
            const float mainArea = ComputeWorldBoundsMainAreaProxy(record.worldBounds);
            if (radius < kShadowCasterMinRadius) {
                return false;
            }
            if (mainArea < kShadowCasterMinMainArea) {
                return false;
            }
            if (record.key.alphaMasked &&
                (radius < kShadowAlphaMaskMinRadius ||
                    mainArea < kShadowAlphaMaskMinMainArea)) {
                return false;
            }
            if (record.key.doubleSided &&
                (radius < kShadowDoubleSidedMinRadius ||
                    mainArea < kShadowDoubleSidedMinMainArea)) {
                return false;
            }
            return true;
        }

        void BuildDepthPrepassOccluderRecords(
            const std::vector<GpuSceneSurfaceRecord>& records,
            const std::vector<uint32_t>& opaqueRecords,
            std::vector<uint32_t>& outOccluders,
            GpuSceneRegistryStats& stats) {

            outOccluders.clear();
            stats.depthPrepassOccluderRecordCount = 0;
            stats.depthPrepassRejectedSmallRecordCount = 0;
            stats.depthPrepassRejectedUnsafeMaterialRecordCount = 0;
            stats.depthPrepassBudgetClippedRecordCount = 0;

            std::vector<DepthPrepassOccluderCandidate> candidates;
            candidates.reserve((std::min)(
                opaqueRecords.size(),
                kDepthVisibilityMaxOccluderRecords * 2u));
            float largestFallbackRadius = -1.0f;
            uint32_t largestFallbackRecord =
                RUNTIME::kInvalidRenderSurfaceIndex;

            for (const uint32_t recordIndex : opaqueRecords) {
                if (recordIndex >= records.size()) {
                    continue;
                }

                const GpuSceneSurfaceRecord& record = records[recordIndex];
                if (!IsGpuSceneForwardOpaqueResidentRecord(record)) {
                    continue;
                }
                if (!HasDepthVisibilitySafeMaterial(record) ||
                    record.key.doubleSided) {
                    ++stats.depthPrepassRejectedUnsafeMaterialRecordCount;
                    continue;
                }

                const float radius = ComputeWorldBoundsRadius(record.worldBounds);
                if (radius < kDepthVisibilityMinOccluderRadius) {
                    ++stats.depthPrepassRejectedSmallRecordCount;
                    if (radius > largestFallbackRadius) {
                        largestFallbackRadius = radius;
                        largestFallbackRecord = recordIndex;
                    }
                    continue;
                }

                candidates.push_back({
                    recordIndex,
                    radius,
                    ComputeDepthOccluderScore(record.worldBounds, radius)
                });
            }

            if (candidates.size() > kDepthVisibilityMaxOccluderRecords) {
                std::sort(
                    candidates.begin(),
                    candidates.end(),
                    [](const DepthPrepassOccluderCandidate& a,
                       const DepthPrepassOccluderCandidate& b) {
                        if (a.score != b.score) {
                            return a.score > b.score;
                        }
                        if (a.radius != b.radius) {
                            return a.radius > b.radius;
                        }
                        return a.recordIndex < b.recordIndex;
                    });
                stats.depthPrepassBudgetClippedRecordCount =
                    ClampToUint32(
                        candidates.size() - kDepthVisibilityMaxOccluderRecords);
                candidates.resize(kDepthVisibilityMaxOccluderRecords);
                std::sort(
                    candidates.begin(),
                    candidates.end(),
                    [](const DepthPrepassOccluderCandidate& a,
                       const DepthPrepassOccluderCandidate& b) {
                        return a.recordIndex < b.recordIndex;
                    });
            }

            outOccluders.reserve(candidates.size());
            for (const DepthPrepassOccluderCandidate& candidate : candidates) {
                outOccluders.push_back(candidate.recordIndex);
            }

            if (outOccluders.empty() &&
                largestFallbackRecord != RUNTIME::kInvalidRenderSurfaceIndex) {
                outOccluders.push_back(largestFallbackRecord);
            }
            stats.depthPrepassOccluderRecordCount =
                ClampToUint32(outOccluders.size());
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

        enum class StaticBatchRunKeyKind {
            Pso,
            Material,
            TextureSet,
            Geometry,
            MeshResource,
            MaterialResource,
            ClusterResource,
        };

        uint64_t PackResourceHandle(uint32_t index, uint32_t generation) {
            return
                (static_cast<uint64_t>(generation) << 32) |
                static_cast<uint64_t>(index);
        }

        uint64_t SelectStaticBatchRunKey(
            const GpuSceneSurfaceRecord& record,
            StaticBatchRunKeyKind kind) {

            const RUNTIME::SurfaceResourceIds& resources = record.key.resources;
            switch (kind) {
            case StaticBatchRunKeyKind::Pso:
                return record.key.psoKey;
            case StaticBatchRunKeyKind::Material:
                return record.key.materialKey;
            case StaticBatchRunKeyKind::TextureSet:
                return record.key.textureSetKey;
            case StaticBatchRunKeyKind::Geometry:
                return record.key.geometryKey;
            case StaticBatchRunKeyKind::MeshResource:
                return PackResourceHandle(
                    resources.mesh.index,
                    resources.mesh.generation);
            case StaticBatchRunKeyKind::MaterialResource:
                return PackResourceHandle(
                    resources.material.index,
                    resources.material.generation);
            case StaticBatchRunKeyKind::ClusterResource:
                return PackResourceHandle(
                    resources.clusterGeometry.index,
                    resources.clusterGeometry.generation);
            default:
                return 0;
            }
        }

        uint32_t CountStaticBatchRuns(
            const std::vector<GpuSceneSurfaceRecord>& records,
            const std::vector<uint32_t>& recordIndices,
            StaticBatchRunKeyKind kind) {

            bool hasPrevious = false;
            uint64_t previous = 0;
            uint32_t runs = 0;
            for (uint32_t recordIndex : recordIndices) {
                if (recordIndex >= records.size()) {
                    continue;
                }
                const GpuSceneSurfaceRecord& record = records[recordIndex];
                if (!record.valid || !record.key.resourceKeyValid) {
                    continue;
                }

                const uint64_t current = SelectStaticBatchRunKey(record, kind);
                if (!hasPrevious || current != previous) {
                    ++runs;
                    previous = current;
                    hasPrevious = true;
                }
            }
            return runs;
        }

        bool ComesBeforeForStaticGpuSceneBatching(
            const GpuSceneSurfaceRecord& lhs,
            const GpuSceneSurfaceRecord& rhs) {

            if (lhs.key.psoKey != rhs.key.psoKey) {
                return lhs.key.psoKey < rhs.key.psoKey;
            }
            if (lhs.key.materialKey != rhs.key.materialKey) {
                return lhs.key.materialKey < rhs.key.materialKey;
            }
            if (lhs.key.textureSetKey != rhs.key.textureSetKey) {
                return lhs.key.textureSetKey < rhs.key.textureSetKey;
            }
            if (lhs.key.geometryBackend != rhs.key.geometryBackend) {
                return lhs.key.geometryBackend < rhs.key.geometryBackend;
            }
            if (lhs.key.geometryKey != rhs.key.geometryKey) {
                return lhs.key.geometryKey < rhs.key.geometryKey;
            }
            const RUNTIME::SurfaceResourceIds& lhsResources = lhs.key.resources;
            const RUNTIME::SurfaceResourceIds& rhsResources = rhs.key.resources;
            const uint64_t lhsMesh =
                PackResourceHandle(lhsResources.mesh.index, lhsResources.mesh.generation);
            const uint64_t rhsMesh =
                PackResourceHandle(rhsResources.mesh.index, rhsResources.mesh.generation);
            if (lhsMesh != rhsMesh) {
                return lhsMesh < rhsMesh;
            }
            const uint64_t lhsMaterial =
                PackResourceHandle(
                    lhsResources.material.index,
                    lhsResources.material.generation);
            const uint64_t rhsMaterial =
                PackResourceHandle(
                    rhsResources.material.index,
                    rhsResources.material.generation);
            if (lhsMaterial != rhsMaterial) {
                return lhsMaterial < rhsMaterial;
            }
            const uint64_t lhsCluster =
                PackResourceHandle(
                    lhsResources.clusterGeometry.index,
                    lhsResources.clusterGeometry.generation);
            const uint64_t rhsCluster =
                PackResourceHandle(
                    rhsResources.clusterGeometry.index,
                    rhsResources.clusterGeometry.generation);
            if (lhsCluster != rhsCluster) {
                return lhsCluster < rhsCluster;
            }
            if (lhs.key.sortKey != rhs.key.sortKey) {
                return lhs.key.sortKey < rhs.key.sortKey;
            }
            return lhs.sourceSurfaceInstanceIndex < rhs.sourceSurfaceInstanceIndex;
        }

        bool HasSameStaticGpuSceneBatchIdentity(
            const GpuSceneSurfaceRecord& lhs,
            const GpuSceneSurfaceRecord& rhs) {

            const RUNTIME::SurfaceResourceIds& lhsResources = lhs.key.resources;
            const RUNTIME::SurfaceResourceIds& rhsResources = rhs.key.resources;
            return
                lhs.key.sortKey == rhs.key.sortKey &&
                lhs.key.psoKey == rhs.key.psoKey &&
                lhs.key.materialKey == rhs.key.materialKey &&
                lhs.key.textureSetKey == rhs.key.textureSetKey &&
                lhs.key.geometryBackend == rhs.key.geometryBackend &&
                lhs.key.geometryKey == rhs.key.geometryKey &&
                lhsResources.mesh == rhsResources.mesh &&
                lhsResources.material == rhsResources.material &&
                lhsResources.clusterGeometry == rhsResources.clusterGeometry;
        }

        GpuSceneStaticBatchStats SortStaticResidentRecordsForBatching(
            const std::vector<GpuSceneSurfaceRecord>& records,
            std::vector<uint32_t>& recordIndices,
            bool allowReorder) {

            GpuSceneStaticBatchStats stats{};
            stats.recordCount = ClampToUint32(recordIndices.size());
            for (uint32_t recordIndex : recordIndices) {
                if (recordIndex >= records.size()) {
                    continue;
                }
                if (records[recordIndex].key.resources.HasPoolHandles()) {
                    ++stats.resourceBackedRecordCount;
                } else {
                    ++stats.missingResourceHandleRecordCount;
                }
            }

            stats.rawPsoRunCount =
                CountStaticBatchRuns(records, recordIndices, StaticBatchRunKeyKind::Pso);
            stats.rawMaterialRunCount =
                CountStaticBatchRuns(records, recordIndices, StaticBatchRunKeyKind::Material);
            stats.rawTextureSetRunCount =
                CountStaticBatchRuns(records, recordIndices, StaticBatchRunKeyKind::TextureSet);
            stats.rawGeometryRunCount =
                CountStaticBatchRuns(records, recordIndices, StaticBatchRunKeyKind::Geometry);
            stats.rawMeshResourceRunCount =
                CountStaticBatchRuns(records, recordIndices, StaticBatchRunKeyKind::MeshResource);
            stats.rawMaterialResourceRunCount =
                CountStaticBatchRuns(
                    records,
                    recordIndices,
                    StaticBatchRunKeyKind::MaterialResource);
            stats.rawClusterResourceRunCount =
                CountStaticBatchRuns(
                    records,
                    recordIndices,
                    StaticBatchRunKeyKind::ClusterResource);

            const std::vector<uint32_t> originalOrder = recordIndices;
            if (allowReorder) {
                std::stable_sort(
                    recordIndices.begin(),
                    recordIndices.end(),
                    [&](uint32_t lhsIndex, uint32_t rhsIndex) {
                        if (lhsIndex >= records.size() || rhsIndex >= records.size()) {
                            return lhsIndex < rhsIndex;
                        }
                        const GpuSceneSurfaceRecord& lhs = records[lhsIndex];
                        const GpuSceneSurfaceRecord& rhs = records[rhsIndex];
                        if (ComesBeforeForStaticGpuSceneBatching(lhs, rhs)) {
                            return true;
                        }
                        if (ComesBeforeForStaticGpuSceneBatching(rhs, lhs)) {
                            return false;
                        }
                        return lhsIndex < rhsIndex;
                    });
                stats.sortApplied = true;
            }

            const size_t compareCount =
                (std::min)(originalOrder.size(), recordIndices.size());
            for (size_t i = 0; i < compareCount; ++i) {
                if (originalOrder[i] != recordIndices[i]) {
                    ++stats.reorderedRecordCount;
                }
            }

            stats.sortedPsoRunCount =
                CountStaticBatchRuns(records, recordIndices, StaticBatchRunKeyKind::Pso);
            stats.sortedMaterialRunCount =
                CountStaticBatchRuns(records, recordIndices, StaticBatchRunKeyKind::Material);
            stats.sortedTextureSetRunCount =
                CountStaticBatchRuns(records, recordIndices, StaticBatchRunKeyKind::TextureSet);
            stats.sortedGeometryRunCount =
                CountStaticBatchRuns(records, recordIndices, StaticBatchRunKeyKind::Geometry);
            stats.sortedMeshResourceRunCount =
                CountStaticBatchRuns(records, recordIndices, StaticBatchRunKeyKind::MeshResource);
            stats.sortedMaterialResourceRunCount =
                CountStaticBatchRuns(
                    records,
                    recordIndices,
                    StaticBatchRunKeyKind::MaterialResource);
            stats.sortedClusterResourceRunCount =
                CountStaticBatchRuns(
                    records,
                    recordIndices,
                    StaticBatchRunKeyKind::ClusterResource);
            return stats;
        }

        template<typename TValue>
        TValue LerpValue(const TValue& a, const TValue& b, float t);

        template<>
        MATH::Vec3 LerpValue(const MATH::Vec3& a, const MATH::Vec3& b, float t) {
            return {
                a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t
            };
        }

        template<>
        MATH::Quat LerpValue(const MATH::Quat& a, const MATH::Quat& b, float t) {
            MATH::Quat end = b;
            const float dot = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
            if (dot < 0.0f) {
                end.x = -end.x;
                end.y = -end.y;
                end.z = -end.z;
                end.w = -end.w;
            }

            const MATH::Quat q{
                a.x + (end.x - a.x) * t,
                a.y + (end.y - a.y) * t,
                a.z + (end.z - a.z) * t,
                a.w + (end.w - a.w) * t
            };
            return MATH::NormalizeQ(q);
        }

        template<typename TKey, typename TValue>
        TValue SampleAnimationKeys(
            const std::vector<TKey>& keys,
            float timeSec,
            AnimationInterpolation interpolation,
            const TValue& fallback) {

            if (keys.empty()) {
                return fallback;
            }
            if (keys.size() == 1 || timeSec <= keys.front().timeSec) {
                return keys.front().value;
            }
            if (timeSec >= keys.back().timeSec) {
                return keys.back().value;
            }

            const auto it = std::lower_bound(
                keys.begin(),
                keys.end(),
                timeSec,
                [](const TKey& key, float value) {
                    return key.timeSec < value;
                });
            if (it == keys.begin()) {
                return it->value;
            }
            const TKey& a = *(it - 1);
            const TKey& b = *it;
            if (interpolation == AnimationInterpolation::Step) {
                return a.value;
            }

            const float span = (std::max)(1e-5f, b.timeSec - a.timeSec);
            const float t = (timeSec - a.timeSec) / span;
            return LerpValue<TValue>(a.value, b.value, t);
        }

        float ResolveAnimationSampleTime(
            const GpuSceneSurfaceRecord& record,
            const AnimationClip& clip) {

            float sampleTime = record.animationTimeSec;
            if (record.animationLoop && clip.durationSec > 0.0001f) {
                sampleTime = std::fmod(sampleTime, clip.durationSec);
                if (sampleTime < 0.0f) {
                    sampleTime += clip.durationSec;
                }
            }
            return sampleTime;
        }

        void BuildAnimatedNodeLocals(
            const GpuSceneSurfaceRecord& record,
            std::vector<Transform3D>& outLocals) {

            outLocals.clear();
            if (record.model == nullptr) {
                return;
            }

            outLocals.reserve(record.model->nodes.size());
            for (const ModelNode& node : record.model->nodes) {
                outLocals.push_back(node.localTransform);
            }

            if (record.animationClipName.empty()) {
                return;
            }
            const AnimationClip* clip =
                record.model->FindAnimationClip(record.animationClipName);
            if (clip == nullptr || clip->channels.empty()) {
                return;
            }

            const float sampleTime = ResolveAnimationSampleTime(record, *clip);
            for (const NodeAnimationChannel& channel : clip->channels) {
                if (channel.targetNode < 0 ||
                    channel.targetNode >= static_cast<int>(outLocals.size())) {
                    continue;
                }

                Transform3D& local = outLocals[static_cast<size_t>(channel.targetNode)];
                if (channel.path == AnimationTargetPath::Translation) {
                    local.position =
                        SampleAnimationKeys<AnimationKeyframe<MATH::Vec3>, MATH::Vec3>(
                            channel.vec3Keys,
                            sampleTime,
                            channel.interpolation,
                            local.position);
                } else if (channel.path == AnimationTargetPath::Scale) {
                    local.scale =
                        SampleAnimationKeys<AnimationKeyframe<MATH::Vec3>, MATH::Vec3>(
                            channel.vec3Keys,
                            sampleTime,
                            channel.interpolation,
                            local.scale);
                } else if (channel.path == AnimationTargetPath::Rotation) {
                    local.rotation =
                        SampleAnimationKeys<AnimationKeyframe<MATH::Quat>, MATH::Quat>(
                            channel.quatKeys,
                            sampleTime,
                            channel.interpolation,
                            local.rotation);
                }
            }
        }

        MATH::Mat4 ResolveNodeLocalMatrix(
            const ModelNode& node,
            const std::vector<Transform3D>& animatedLocals,
            size_t nodeIndex) {

            if (node.hasLocalMatrix) {
                return node.localMatrix;
            }
            if (nodeIndex < animatedLocals.size()) {
                return animatedLocals[nodeIndex].GetLocalMatrix();
            }
            return node.localTransform.GetLocalMatrix();
        }

        void EvaluateNodeMatrixRecursive(
            const ModelAsset& model,
            const std::vector<Transform3D>& animatedLocals,
            int nodeIndex,
            const MATH::Mat4& parentWorld,
            std::vector<MATH::Mat4>& outGlobals,
            std::vector<uint8_t>& visited) {

            if (nodeIndex < 0 || nodeIndex >= static_cast<int>(model.nodes.size())) {
                return;
            }
            const size_t index = static_cast<size_t>(nodeIndex);
            if (visited[index]) {
                return;
            }

            const ModelNode& node = model.nodes[index];
            outGlobals[index] =
                parentWorld * ResolveNodeLocalMatrix(node, animatedLocals, index);
            visited[index] = 1u;

            for (int childIndex : node.children) {
                EvaluateNodeMatrixRecursive(
                    model,
                    animatedLocals,
                    childIndex,
                    outGlobals[index],
                    outGlobals,
                    visited);
            }
        }

        void BuildNodeGlobalMatricesWithRoot(
            const ModelAsset& model,
            const std::vector<Transform3D>& animatedLocals,
            const MATH::Mat4& rootWorld,
            std::vector<MATH::Mat4>& outGlobals,
            std::vector<uint8_t>& visited) {

            if (model.nodes.empty()) {
                outGlobals.clear();
                visited.clear();
                return;
            }

            outGlobals.assign(model.nodes.size(), rootWorld);
            visited.assign(model.nodes.size(), 0u);

            for (size_t i = 0; i < model.nodes.size(); ++i) {
                if (model.nodes[i].parent == -1) {
                    EvaluateNodeMatrixRecursive(
                        model,
                        animatedLocals,
                        static_cast<int>(i),
                        rootWorld,
                        outGlobals,
                        visited);
                }
            }
            for (size_t i = 0; i < model.nodes.size(); ++i) {
                if (visited[i]) {
                    continue;
                }
                const int parent = model.nodes[i].parent;
                const MATH::Mat4 parentWorld =
                    parent >= 0 && parent < static_cast<int>(outGlobals.size())
                        ? outGlobals[static_cast<size_t>(parent)]
                        : rootWorld;
                EvaluateNodeMatrixRecursive(
                    model,
                    animatedLocals,
                    static_cast<int>(i),
                    parentWorld,
                    outGlobals,
                    visited);
            }
        }

        struct SkinPoseBuildEntry {
            RUNTIME::SceneRenderObjectId objectId{};
            uint64_t objectVersion = 0;
            const ModelAsset* model = nullptr;
            std::vector<Transform3D> animatedLocals{};
            std::vector<MATH::Mat4> localNodeGlobals{};
            std::vector<uint8_t> visited{};
            std::unordered_map<int, std::vector<MATH::Mat4>> palettesBySkin{};
        };

        struct SkinPoseBuildCache {
            std::vector<SkinPoseBuildEntry> entries{};
        };

        bool BuildJointPalette(
            const ModelAsset& model,
            int skinIndex,
            const std::vector<MATH::Mat4>& localNodeGlobals,
            std::vector<MATH::Mat4>& outPalette) {

            outPalette.clear();
            const SkeletonAsset* skin = model.FindSkin(skinIndex);
            if (skin == nullptr) {
                return false;
            }

            outPalette.resize(skin->joints.size(), MATH::Mat4::Identity());
            for (size_t jointIndex = 0; jointIndex < skin->joints.size(); ++jointIndex) {
                const SkeletonJoint& joint = skin->joints[jointIndex];
                if (joint.nodeIndex < 0 ||
                    joint.nodeIndex >= static_cast<int>(localNodeGlobals.size())) {
                    continue;
                }
                outPalette[jointIndex] =
                    localNodeGlobals[static_cast<size_t>(joint.nodeIndex)] *
                    joint.inverseBindMatrix;
            }
            return true;
        }

        SkinPoseBuildEntry* ResolveSkinPoseEntry(
            const GpuSceneSurfaceRecord& record,
            SkinPoseBuildCache& cache) {

            if (record.model == nullptr) {
                return nullptr;
            }

            for (SkinPoseBuildEntry& entry : cache.entries) {
                if (entry.objectId == record.objectId &&
                    entry.objectVersion == record.objectVersion &&
                    entry.model == record.model) {
                    return &entry;
                }
            }

            SkinPoseBuildEntry entry{};
            entry.objectId = record.objectId;
            entry.objectVersion = record.objectVersion;
            entry.model = record.model;
            BuildAnimatedNodeLocals(record, entry.animatedLocals);
            BuildNodeGlobalMatricesWithRoot(
                *record.model,
                entry.animatedLocals,
                MATH::Mat4::Identity(),
                entry.localNodeGlobals,
                entry.visited);

            cache.entries.push_back(std::move(entry));
            return &cache.entries.back();
        }

        const std::vector<MATH::Mat4>* ResolveJointPalette(
            const GpuSceneSurfaceRecord& record,
            SkinPoseBuildCache& cache) {

            if (record.model == nullptr || record.surface == nullptr) {
                return nullptr;
            }
            const int skinIndex = record.surface->skinIndex;
            if (skinIndex < 0) {
                return nullptr;
            }

            SkinPoseBuildEntry* entry = ResolveSkinPoseEntry(record, cache);
            if (entry == nullptr) {
                return nullptr;
            }

            auto found = entry->palettesBySkin.find(skinIndex);
            if (found != entry->palettesBySkin.end()) {
                return &found->second;
            }

            std::vector<MATH::Mat4>& palette = entry->palettesBySkin[skinIndex];
            if (!BuildJointPalette(
                *record.model,
                skinIndex,
                entry->localNodeGlobals,
                palette)) {
                entry->palettesBySkin.erase(skinIndex);
                return nullptr;
            }
            return &palette;
        }

        RUNTIME::SurfaceDrawIndexedArgs BuildDrawIndexedArgs(
            const GpuSceneSurfaceRecord& record) {

            RUNTIME::SurfaceDrawIndexedArgs args{};
            if (record.model == nullptr ||
                record.meshIndex >= record.model->meshes.size()) {
                return args;
            }
            const MeshAsset& mesh = record.model->meshes[record.meshIndex];
            if (record.primitiveIndex >= mesh.primitives.size()) {
                return args;
            }
            args.indexCountPerInstance =
                ClampToUint32(mesh.primitives[record.primitiveIndex].indices.size());
            args.instanceCount = 1u;
            return args;
        }

        bool IsValidDrawArgs(const RUNTIME::SurfaceDrawIndexedArgs& args) {
            return args.indexCountPerInstance > 0u && args.instanceCount > 0u;
        }

        bool HasValidGpuSceneSubmitPrimitiveTarget(
            const GpuSceneSurfaceRecord& record) {

            return
                record.model != nullptr &&
                record.meshIndex != RUNTIME::kInvalidRenderSurfaceIndex &&
                record.primitiveIndex != RUNTIME::kInvalidRenderSurfaceIndex &&
                record.meshIndex < record.model->meshes.size() &&
                record.primitiveIndex <
                    record.model->meshes[record.meshIndex].primitives.size();
        }

        void AppendSkinnedTraditionalRecord(
            const std::vector<GpuSceneSurfaceRecord>& records,
            uint32_t recordIndex,
            RUNTIME::SurfaceDrawCommandPass pass,
            GpuSceneRegistry::TraditionalSkinnedStream& stream,
            SkinPoseBuildCache& poseCache) {

            if (recordIndex >= records.size()) {
                return;
            }
            const GpuSceneSurfaceRecord& record = records[recordIndex];
            const std::vector<MATH::Mat4>* palette =
                ResolveJointPalette(record, poseCache);
            if (palette == nullptr || palette->empty()) {
                return;
            }

            const uint32_t recordIndexInStream = ClampToUint32(stream.records.size());
            const uint32_t executableIndex =
                ClampToUint32(stream.executableRecordIndices.size());
            const uint32_t gpuSceneInstanceIndex =
                ClampToUint32(stream.instances.size());

            stream.records.push_back(record);
            stream.executableRecordIndices.push_back(recordIndexInStream);
            stream.jointPalettes.push_back(*palette);

            std::vector<uint32_t> singleRecordIndex{ recordIndex };
            std::vector<RUNTIME::SurfaceGpuSceneInstance> singleInstance{};
            std::vector<RUNTIME::SurfaceGpuSceneMaterialSource> singleMaterial{};
            (void)BuildGpuSceneInstanceList(
                records,
                singleRecordIndex,
                singleInstance,
                singleMaterial);
            if (!singleInstance.empty()) {
                singleInstance.front().sourceRecordIndex = recordIndexInStream;
                stream.instances.push_back(singleInstance.front());
            }
            if (!singleMaterial.empty()) {
                stream.materialSources.push_back(singleMaterial.front());
            }

            RUNTIME::SurfaceDrawCommand command{};
            command.pass = pass;
            command.firstExecutableIndex = executableIndex;
            command.recordCount = 1u;
            command.firstRecordIndex = recordIndexInStream;
            command.firstGpuSceneInstanceIndex = gpuSceneInstanceIndex;
            command.gpuSceneInstanceCount = 1u;
            command.batchKey = RUNTIME::BuildSurfaceDrawBatchKey(
                pass,
                stream.records.back().key);
            command.resources = stream.records.back().key.resources;
            command.psoKey = stream.records.back().key.psoKey;
            command.geometryKey = stream.records.back().key.geometryKey;
            command.geometryBackend = stream.records.back().key.geometryBackend;
            command.materialKey = stream.records.back().key.materialKey;
            command.textureSetKey = stream.records.back().key.textureSetKey;
            command.modelKey = stream.records.back().key.modelKey;
            command.singleRecord = true;
            command.transparent = stream.records.back().key.transparent;
            command.alphaMasked = stream.records.back().key.alphaMasked;
            command.doubleSided = stream.records.back().key.doubleSided;
            command.clusterMainlineEligible = false;
            command.drawArgs = BuildDrawIndexedArgs(record);
            command.drawArgsValid = IsValidDrawArgs(command.drawArgs);
            stream.commands.push_back(command);
        }

        void AttachTraditionalSkinnedView(
            GpuDrivenPassSource& pass,
            const GpuSceneRegistry::TraditionalSkinnedStream& stream,
            uint32_t gpuSceneBaseIndex) {

            pass.traditionalIndirect.Reset();
            if (!stream.HasCommands()) {
                return;
            }
            pass.traditionalIndirect.records = &stream.records;
            pass.traditionalIndirect.executableRecordIndices =
                &stream.executableRecordIndices;
            pass.traditionalIndirect.commands = &stream.commands;
            pass.traditionalIndirect.instances = &stream.instances;
            pass.traditionalIndirect.materialSources = &stream.materialSources;
            pass.traditionalIndirect.jointPalettes = &stream.jointPalettes;
            pass.traditionalIndirect.gpuSceneBaseIndex = gpuSceneBaseIndex;
            pass.traditionalIndirect.gpuSceneInstanceCount =
                ClampToUint32(stream.instances.size());
        }

        bool IsGpuSceneSkinnedTraditionalRecordCommon(
            const GpuSceneSurfaceRecord& record) {

            return
                record.valid &&
                record.key.resourceKeyValid &&
                record.skinned &&
                !record.hasSpecialRenderDebug &&
                HasValidGpuSceneSubmitPrimitiveTarget(record);
        }

        bool IsGpuSceneForwardSkinnedTraditionalRecord(
            const GpuSceneSurfaceRecord& record) {

            return
                record.forwardCandidate &&
                IsGpuSceneSkinnedTraditionalRecordCommon(record);
        }

        bool IsGpuSceneShadowSkinnedTraditionalRecord(
            const GpuSceneSurfaceRecord& record) {

            return
                record.shadowCandidate &&
                IsGpuSceneSkinnedTraditionalRecordCommon(record) &&
                !record.key.transparent;
        }
    }

    void GpuSceneRegistry::TraditionalSkinnedStream::Clear() {
        records.clear();
        executableRecordIndices.clear();
        commands.clear();
        instances.clear();
        materialSources.clear();
        jointPalettes.clear();
    }

    bool GpuSceneRegistry::TraditionalSkinnedStream::HasCommands() const {
        return
            !records.empty() &&
            !commands.empty() &&
            !executableRecordIndices.empty() &&
            records.size() == jointPalettes.size();
    }

    void GpuSceneRegistry::Clear() {
        surfaceRecords_.clear();
        forwardOpaqueResidentRecordIndices_.clear();
        depthPrepassOccluderRecordIndices_.clear();
        forwardDepthAwareResidentRecordIndices_.clear();
        forwardTransparentResidentRecordIndices_.clear();
        shadowResidentRecordIndices_.clear();
        forwardOpaqueSkinnedRecordIndices_.clear();
        forwardDepthAwareSkinnedRecordIndices_.clear();
        forwardTransparentSkinnedRecordIndices_.clear();
        shadowSkinnedRecordIndices_.clear();
        forwardOpaqueGpuSceneIndexByRecord_.clear();
        depthPrepassGpuSceneIndexByRecord_.clear();
        forwardDepthAwareGpuSceneIndexByRecord_.clear();
        forwardTransparentGpuSceneIndexByRecord_.clear();
        shadowGpuSceneIndexByRecord_.clear();
        forwardOpaqueGpuSceneInstances_.clear();
        forwardOpaqueMaterialSources_.clear();
        depthPrepassGpuSceneInstances_.clear();
        depthPrepassMaterialSources_.clear();
        forwardDepthAwareGpuSceneInstances_.clear();
        forwardDepthAwareMaterialSources_.clear();
        forwardTransparentGpuSceneInstances_.clear();
        forwardTransparentMaterialSources_.clear();
        shadowGpuSceneInstances_.clear();
        shadowMaterialSources_.clear();
        forwardOpaqueSkinnedStream_.Clear();
        forwardDepthAwareSkinnedStream_.Clear();
        forwardTransparentSkinnedStream_.Clear();
        shadowSkinnedStream_.Clear();
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
            surfaceRecords_.push_back(std::move(record));
        }

        stats_.sourceRecordCount = ClampToUint32(surfaceRecords_.size());

        std::vector<uint32_t> routedRecordIndices{};
        routedRecordIndices.reserve(surfaceRecords_.size());
        for (uint32_t recordIndex = 0;
            recordIndex < surfaceRecords_.size();
            ++recordIndex) {

            const GpuSceneSurfaceRecord& record = surfaceRecords_[recordIndex];
            if (record.valid && record.shadowCandidate && record.key.resourceKeyValid) {
                if (IsGpuSceneStaticShadowCasterRecord(record)) {
                    shadowResidentRecordIndices_.push_back(recordIndex);
                } else if (IsGpuSceneShadowSkinnedTraditionalRecord(record)) {
                    shadowSkinnedRecordIndices_.push_back(recordIndex);
                } else {
                    ++stats_.blockedShadowRecordCount;
                }
            }
            if (record.valid && record.forwardCandidate && record.key.resourceKeyValid) {
                routedRecordIndices.push_back(recordIndex);
            }
        }
        stats_.forwardRoutedRecordCount = ClampToUint32(routedRecordIndices.size());

        forwardOpaqueResidentRecordIndices_.reserve(routedRecordIndices.size());
        forwardDepthAwareResidentRecordIndices_.reserve(routedRecordIndices.size());
        forwardTransparentResidentRecordIndices_.reserve(routedRecordIndices.size());
        for (const uint32_t recordIndex : routedRecordIndices) {
            if (recordIndex >= surfaceRecords_.size()) {
                continue;
            }

            const GpuSceneSurfaceRecord& record = surfaceRecords_[recordIndex];
            if (IsGpuSceneForwardOpaqueResidentRecord(record)) {
                forwardOpaqueResidentRecordIndices_.push_back(recordIndex);
                ++stats_.forwardOpaqueClusterCandidateRecordCount;
            } else if (IsGpuSceneForwardDepthAwareResidentRecord(record)) {
                forwardDepthAwareResidentRecordIndices_.push_back(recordIndex);
            } else if (IsGpuSceneForwardTransparentResidentRecord(record)) {
                forwardTransparentResidentRecordIndices_.push_back(recordIndex);
            } else if (IsGpuSceneForwardSkinnedTraditionalRecord(record)) {
                if (record.key.depthAware) {
                    forwardDepthAwareSkinnedRecordIndices_.push_back(recordIndex);
                } else if (record.key.transparent) {
                    forwardTransparentSkinnedRecordIndices_.push_back(recordIndex);
                } else {
                    forwardOpaqueSkinnedRecordIndices_.push_back(recordIndex);
                }
            } else if (record.forwardCandidate) {
                ++stats_.unsupportedForwardRecordCount;
                if (record.key.depthAware) {
                    ++stats_.blockedForwardDepthAwareRecordCount;
                } else if (record.key.transparent) {
                    ++stats_.blockedForwardTransparentRecordCount;
                }
            }
        }
        stats_.forwardOpaqueResidentRecordCount =
            ClampToUint32(forwardOpaqueResidentRecordIndices_.size());
        stats_.forwardSkinnedTraditionalRecordCount =
            ClampToUint32(
                forwardOpaqueSkinnedRecordIndices_.size() +
                forwardDepthAwareSkinnedRecordIndices_.size() +
                forwardTransparentSkinnedRecordIndices_.size());
        stats_.shadowSkinnedTraditionalRecordCount =
            ClampToUint32(shadowSkinnedRecordIndices_.size());
        stats_.strictMainlineBlockedRecordCount =
            stats_.blockedForwardDepthAwareRecordCount +
            stats_.blockedForwardTransparentRecordCount +
            stats_.blockedShadowRecordCount;

        stats_.forwardOpaqueBatchStats =
            SortStaticResidentRecordsForBatching(
                surfaceRecords_,
                forwardOpaqueResidentRecordIndices_,
                true);
        BuildDepthPrepassOccluderRecords(
            surfaceRecords_,
            forwardOpaqueResidentRecordIndices_,
            depthPrepassOccluderRecordIndices_,
            stats_);
        stats_.forwardDepthAwareBatchStats =
            SortStaticResidentRecordsForBatching(
                surfaceRecords_,
                forwardDepthAwareResidentRecordIndices_,
                true);
        stats_.forwardTransparentBatchStats =
            SortStaticResidentRecordsForBatching(
                surfaceRecords_,
                forwardTransparentResidentRecordIndices_,
                false);
        stats_.shadowBatchStats =
            SortStaticResidentRecordsForBatching(
                surfaceRecords_,
                shadowResidentRecordIndices_,
                true);

        const auto buildPass =
            [&](const std::vector<uint32_t>& recordIndices,
                std::vector<uint32_t>& indexByRecord,
                std::vector<RUNTIME::SurfaceGpuSceneInstance>& instances,
                std::vector<RUNTIME::SurfaceGpuSceneMaterialSource>& materialSources) {
            indexByRecord.assign(
                surfaceRecords_.size(),
                RUNTIME::kInvalidRenderSurfaceIndex);
            RUNTIME::SurfaceGpuSceneBuildStats passStats =
                BuildGpuSceneInstanceList(
                surfaceRecords_,
                    recordIndices,
                    instances,
                    materialSources);
            for (uint32_t localInstanceIndex = 0;
                localInstanceIndex < recordIndices.size();
                ++localInstanceIndex) {

                const uint32_t recordIndex = recordIndices[localInstanceIndex];
                if (recordIndex < indexByRecord.size()) {
                    indexByRecord[recordIndex] = localInstanceIndex;
                }
            }
            return passStats;
        };

        stats_.forwardOpaqueGpuSceneStats =
            buildPass(
                forwardOpaqueResidentRecordIndices_,
                forwardOpaqueGpuSceneIndexByRecord_,
                forwardOpaqueGpuSceneInstances_,
                forwardOpaqueMaterialSources_);
        stats_.depthPrepassGpuSceneStats =
            buildPass(
                depthPrepassOccluderRecordIndices_,
                depthPrepassGpuSceneIndexByRecord_,
                depthPrepassGpuSceneInstances_,
                depthPrepassMaterialSources_);
        stats_.forwardDepthAwareGpuSceneStats =
            buildPass(
                forwardDepthAwareResidentRecordIndices_,
                forwardDepthAwareGpuSceneIndexByRecord_,
                forwardDepthAwareGpuSceneInstances_,
                forwardDepthAwareMaterialSources_);
        stats_.forwardTransparentGpuSceneStats =
            buildPass(
                forwardTransparentResidentRecordIndices_,
                forwardTransparentGpuSceneIndexByRecord_,
                forwardTransparentGpuSceneInstances_,
                forwardTransparentMaterialSources_);
        (void)buildPass(
            shadowResidentRecordIndices_,
            shadowGpuSceneIndexByRecord_,
            shadowGpuSceneInstances_,
            shadowMaterialSources_);

        const auto buildSkinnedStream =
            [&](const std::vector<uint32_t>& recordIndices,
                RUNTIME::SurfaceDrawCommandPass pass,
                TraditionalSkinnedStream& stream) {
            stream.Clear();
            SkinPoseBuildCache poseCache{};
            for (const uint32_t recordIndex : recordIndices) {
                AppendSkinnedTraditionalRecord(
                    surfaceRecords_,
                    recordIndex,
                    pass,
                    stream,
                    poseCache);
            }

            RUNTIME::SurfaceGpuSceneBuildStats streamStats{};
            streamStats.commandCount = ClampToUint32(stream.commands.size());
            streamStats.instanceCount = ClampToUint32(stream.instances.size());
            streamStats.maxCommandInstanceCount = stream.instances.empty() ? 0u : 1u;
            for (const RUNTIME::SurfaceGpuSceneInstance& instance : stream.instances) {
                if ((instance.resourceFlags &
                    static_cast<uint32_t>(RUNTIME::SurfaceGpuSceneResourceFlags::Mesh)) != 0u &&
                    (instance.resourceFlags &
                    static_cast<uint32_t>(RUNTIME::SurfaceGpuSceneResourceFlags::Material)) != 0u) {
                    ++streamStats.resourceBackedInstanceCount;
                } else {
                    ++streamStats.missingResourceHandleInstanceCount;
                }
            }
            return streamStats;
        };

        stats_.forwardOpaqueSkinnedTraditionalGpuSceneStats =
            buildSkinnedStream(
                forwardOpaqueSkinnedRecordIndices_,
                RUNTIME::SurfaceDrawCommandPass::Forward,
                forwardOpaqueSkinnedStream_);
        stats_.forwardDepthAwareSkinnedTraditionalGpuSceneStats =
            buildSkinnedStream(
                forwardDepthAwareSkinnedRecordIndices_,
                RUNTIME::SurfaceDrawCommandPass::DepthAware,
                forwardDepthAwareSkinnedStream_);
        stats_.forwardTransparentSkinnedTraditionalGpuSceneStats =
            buildSkinnedStream(
                forwardTransparentSkinnedRecordIndices_,
                RUNTIME::SurfaceDrawCommandPass::Forward,
                forwardTransparentSkinnedStream_);
        stats_.shadowSkinnedTraditionalGpuSceneStats =
            buildSkinnedStream(
                shadowSkinnedRecordIndices_,
                RUNTIME::SurfaceDrawCommandPass::Shadow,
                shadowSkinnedStream_);

        layoutVersion_ = input.sceneCache->GetSurfaceVersion();
        routingVersion_ = input.sceneCache->GetSurfaceRoutingVersion();
        dataVersion_ = input.sceneCache->GetSurfaceDataVersion();
        sourceSurfaceCount_ = ClampToUint32(surfaces.size());
        SuppressCpuForwardViews(input);
        RebuildForwardSceneSource();
    }

    void GpuSceneRegistry::SuppressCpuForwardViews(
        const GpuSceneRegistrySyncInput& input) {

        stats_.strictGpuDrivenMainline = true;
        stats_.cpuForwardViewSuppressedCount =
            input.sceneCache != nullptr ? 1u : 0u;
        stats_.forwardOpaqueTraditionalGpuSceneStats = {};
        stats_.forwardDepthAwareTraditionalGpuSceneStats = {};
        stats_.forwardTransparentTraditionalGpuSceneStats = {};
    }

    bool GpuSceneRegistry::TryPatchForwardDataFromSceneCache(
        const GpuSceneRegistrySyncInput& input) {

        if (input.sceneCache == nullptr) {
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
            if (IsGpuSceneForwardSkinnedTraditionalRecord(oldRecord) ||
                IsGpuSceneForwardSkinnedTraditionalRecord(newRecord) ||
                IsGpuSceneShadowSkinnedTraditionalRecord(oldRecord) ||
                IsGpuSceneShadowSkinnedTraditionalRecord(newRecord)) {
                return false;
            }
            const bool oldOpaque = IsGpuSceneForwardOpaqueResidentRecord(oldRecord);
            const bool newOpaque = IsGpuSceneForwardOpaqueResidentRecord(newRecord);
            const bool oldDepthOccluder =
                IsDepthVisibilityOccluderRecord(oldRecord, true);
            const bool newDepthOccluder =
                IsDepthVisibilityOccluderRecord(newRecord, true);
            const bool oldNonOpaqueGpuPass =
                IsGpuSceneForwardDepthAwareResidentRecord(oldRecord) ||
                IsGpuSceneForwardTransparentResidentRecord(oldRecord) ||
                IsGpuSceneStaticShadowCasterRecord(oldRecord);
            const bool newNonOpaqueGpuPass =
                IsGpuSceneForwardDepthAwareResidentRecord(newRecord) ||
                IsGpuSceneForwardTransparentResidentRecord(newRecord) ||
                IsGpuSceneStaticShadowCasterRecord(newRecord);
            if (oldNonOpaqueGpuPass || newNonOpaqueGpuPass) {
                return false;
            }
            if (oldOpaque != newOpaque ||
                oldDepthOccluder != newDepthOccluder ||
                !HasSameStaticGpuSceneBatchIdentity(oldRecord, newRecord)) {
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
            if (newDepthOccluder) {
                if (surfaceIndex >= depthPrepassGpuSceneIndexByRecord_.size()) {
                    return false;
                }
                const uint32_t depthGpuSceneInstanceIndex =
                    depthPrepassGpuSceneIndexByRecord_[surfaceIndex];
                if (depthGpuSceneInstanceIndex == RUNTIME::kInvalidRenderSurfaceIndex ||
                    depthGpuSceneInstanceIndex >= depthPrepassGpuSceneInstances_.size() ||
                    depthGpuSceneInstanceIndex >= depthPrepassMaterialSources_.size()) {
                    return false;
                }
                depthPrepassGpuSceneInstances_[depthGpuSceneInstanceIndex] =
                    singleInstance[0];
                depthPrepassMaterialSources_[depthGpuSceneInstanceIndex] =
                    singleMaterial[0];
                AppendDirtyRange(
                    sceneSource_.GetPass(GpuDrivenPassKind::DepthPrepass).dirtyRanges,
                    depthGpuSceneInstanceIndex,
                    1u);
            }
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
        AttachTraditionalSkinnedView(
            sceneSource_.GetPass(GpuDrivenPassKind::ForwardOpaque),
            forwardOpaqueSkinnedStream_,
            cursor);
        cursor += ClampToUint32(forwardOpaqueSkinnedStream_.instances.size());

        ResetPassSource(
            sceneSource_.GetPass(GpuDrivenPassKind::DepthPrepass),
            &depthPrepassGpuSceneInstances_,
            &depthPrepassMaterialSources_,
            cursor,
            GpuDrivenBackendKind::MeshShader,
            true);
        cursor += ClampToUint32(depthPrepassGpuSceneInstances_.size());

        ResetPassSource(
            sceneSource_.GetPass(GpuDrivenPassKind::ForwardDepthAware),
            &forwardDepthAwareGpuSceneInstances_,
            &forwardDepthAwareMaterialSources_,
            cursor,
            GpuDrivenBackendKind::MeshShader,
            true);
        cursor += ClampToUint32(forwardDepthAwareGpuSceneInstances_.size());
        AttachTraditionalSkinnedView(
            sceneSource_.GetPass(GpuDrivenPassKind::ForwardDepthAware),
            forwardDepthAwareSkinnedStream_,
            cursor);
        cursor += ClampToUint32(forwardDepthAwareSkinnedStream_.instances.size());

        ResetPassSource(
            sceneSource_.GetPass(GpuDrivenPassKind::ForwardTransparent),
            &forwardTransparentGpuSceneInstances_,
            &forwardTransparentMaterialSources_,
            cursor,
            GpuDrivenBackendKind::MeshShader,
            true);
        cursor += ClampToUint32(forwardTransparentGpuSceneInstances_.size());
        AttachTraditionalSkinnedView(
            sceneSource_.GetPass(GpuDrivenPassKind::ForwardTransparent),
            forwardTransparentSkinnedStream_,
            cursor);
        cursor += ClampToUint32(forwardTransparentSkinnedStream_.instances.size());

        ResetPassSource(
            sceneSource_.GetPass(GpuDrivenPassKind::Shadow),
            &shadowGpuSceneInstances_,
            &shadowMaterialSources_,
            cursor,
            GpuDrivenBackendKind::MeshShader,
            true);
        cursor += ClampToUint32(shadowGpuSceneInstances_.size());
        AttachTraditionalSkinnedView(
            sceneSource_.GetPass(GpuDrivenPassKind::Shadow),
            shadowSkinnedStream_,
            cursor);
        cursor += ClampToUint32(shadowSkinnedStream_.instances.size());

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

    const GpuDrivenSceneSource& GpuSceneRegistry::GetSceneSource() const {
        return sceneSource_;
    }

    bool GpuSceneRegistry::HasShadowPassSource() const {
        const GpuDrivenPassSource& shadow =
            sceneSource_.GetPass(GpuDrivenPassKind::Shadow);
        return shadow.HasPrimaryGpuSceneInstances();
    }

    const std::vector<GpuSceneSurfaceRecord>& GpuSceneRegistry::GetSurfaceRecords() const {
        return surfaceRecords_;
    }

    const GpuSceneRegistryStats& GpuSceneRegistry::GetStats() const {
        return stats_;
    }

} // namespace HIKARI::RENDER3D::GPUDRIVEN
