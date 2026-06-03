#include "Render3D/Runtime/HIKARI_StaticDrawRecordCache.h"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>

#include "Render3D/Core/HIKARI_BoundsUtils.h"
#include "Render3D/Runtime/HIKARI_RenderSurfaceResolver.h"

namespace HIKARI::RENDER3D::RUNTIME {

    namespace {
        uint32_t ClampToUint32(size_t value) {
            return static_cast<uint32_t>(
                (std::min)(value, static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
        }

        void CopyMaterialFxValues(const SceneRenderObjectDesc& desc, StaticDrawRecord& record) {
            for (int i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                record.materialFxParamValues[i] = desc.materialFxParamValues[i];
            }
        }

        bool IsStaticCacheCandidate(const SceneRenderObject& object) {
            // 無効な空間情報は後段の draw record に渡さない。
            return
                object.valid &&
                object.desc.id.IsValid() &&
                object.desc.model != nullptr &&
                BOUNDS::IsUsable(object.desc.localBounds) &&
                BOUNDS::IsUsable(object.desc.worldBounds);
        }
    }

    void StaticDrawRecordCache::Clear() {
        entries_.clear();
        activeObjectIds_.clear();
        records_.clear();
        stats_ = {};
    }

    void StaticDrawRecordCache::SyncFromSceneRenderCache(const SceneRenderCache& sceneCache) {
        stats_ = {};
        activeObjectIds_.clear();
        std::unordered_set<uint64_t> activeEntryIds{};
        activeObjectIds_.reserve(sceneCache.GetObjects().size());
        activeEntryIds.reserve(sceneCache.GetObjects().size());

        for (const SceneRenderObject& object : sceneCache.GetObjects()) {
            if (!object.desc.isStatic) {
                ++stats_.skippedDynamicObjectCount;
                continue;
            }

            ++stats_.staticObjectCount;

            if (!object.desc.visible) {
                ++stats_.skippedInvisibleObjectCount;
                continue;
            }
            if (!IsStaticCacheCandidate(object)) {
                ++stats_.skippedInvalidObjectCount;
                continue;
            }
            if (object.desc.renderModel == nullptr || !object.desc.renderModel->valid) {
                ++stats_.skippedInvalidRenderModelCount;
                continue;
            }

            activeEntryIds.insert(object.desc.id.value);
            activeObjectIds_.push_back(object.desc.id.value);
            auto [entryIt, inserted] = entries_.try_emplace(object.desc.id.value);
            StaticDrawObjectEntry& entry = entryIt->second;

            if (!inserted && entry.valid && entry.sourceVersion == object.version) {
                ++stats_.reusedObjectCount;
                continue;
            }

            RebuildObjectRecords(object, entry);
            ++stats_.rebuiltObjectCount;
        }

        for (auto it = entries_.begin(); it != entries_.end();) {
            if (activeEntryIds.find(it->first) == activeEntryIds.end()) {
                it = entries_.erase(it);
                ++stats_.removedObjectCount;
            } else {
                ++it;
            }
        }

        RebuildFlatRecordList();
    }

    const std::vector<StaticDrawRecord>& StaticDrawRecordCache::GetRecords() const {
        return records_;
    }

    const StaticDrawRecordCache::Stats& StaticDrawRecordCache::GetStats() const {
        return stats_;
    }

    bool StaticDrawRecordCache::HasValidRecordsForObject(SceneRenderObjectId objectId) const {
        if (!objectId.IsValid()) {
            return false;
        }
        return std::any_of(records_.begin(), records_.end(), [objectId](const StaticDrawRecord& record) {
            return record.objectId == objectId;
        });
    }

    bool StaticDrawRecordCache::HasFullForwardCoverageForObject(SceneRenderObjectId objectId) const {
        if (!objectId.IsValid()) {
            return false;
        }
        const auto found = entries_.find(objectId.value);
        if (found == entries_.end()) {
            return false;
        }

        const StaticDrawObjectEntry& entry = found->second;
        return
            entry.valid &&
            entry.coverageStatus == StaticDrawCoverageStatus::Full &&
            entry.validForwardRecordCount > 0;
    }

    StaticDrawCoverageStatus StaticDrawRecordCache::GetCoverageStatusForObject(SceneRenderObjectId objectId) const {
        if (!objectId.IsValid()) {
            return StaticDrawCoverageStatus::None;
        }
        const auto found = entries_.find(objectId.value);
        if (found == entries_.end()) {
            return StaticDrawCoverageStatus::None;
        }
        return found->second.coverageStatus;
    }

    StaticDrawRecordValidationResult StaticDrawRecordCache::ValidateStaticDrawRecord(const StaticDrawRecord& record) const {
        StaticDrawRecordValidationResult result{};
        if (!record.objectId.IsValid() || record.model == nullptr) {
            result.invalidObject = true;
        }
        if (record.renderModel == nullptr || !record.renderModel->valid) {
            result.invalidRenderModel = true;
        }
        if (!record.hasDrawWorldMatrix) {
            result.missingDrawMatrix = true;
        }
        if (!BOUNDS::IsUsable(record.worldBounds)) {
            result.invalidBounds = true;
        }

        bool primitiveIndexValid = false;
        if (record.renderModel != nullptr &&
            record.submeshIndex < record.renderModel->surfaces.size()) {
            primitiveIndexValid = HasValidRenderSurfacePrimitive(
                record.model,
                record.renderModel->surfaces[record.submeshIndex]);
        }
        if (!primitiveIndexValid) {
            result.invalidPrimitiveIndex = true;
        }

        return result;
    }

    void StaticDrawRecordCache::RebuildObjectRecords(const SceneRenderObject& object, StaticDrawObjectEntry& entry) {
        entry.objectId = object.desc.id;
        entry.sourceVersion = object.version;
        entry.valid = true;
        entry.expectedForwardSubmeshCount = 0;
        entry.validForwardRecordCount = 0;
        entry.skippedSkinnedSubmeshCount = 0;
        entry.skippedInvalidRecordCount = 0;
        entry.skippedUnsupportedSubmeshCount = 0;
        entry.invalidRecordBoundsCount = 0;
        entry.missingDrawMatrixCount = 0;
        entry.invalidPrimitiveIndexCount = 0;
        entry.skippedAnimatedObject = false;
        entry.skippedDebugModeObject = false;
        entry.coverageStatus = StaticDrawCoverageStatus::None;
        entry.records.clear();

        const RenderModelAsset& renderModel = *object.desc.renderModel;
        if (!object.desc.allowStaticCachedForward) {
            entry.expectedForwardSubmeshCount = ClampToUint32(renderModel.surfaces.size());
            entry.skippedAnimatedObject = object.desc.hasRuntimeAnimation;
            entry.skippedDebugModeObject = object.desc.hasSpecialRenderDebug;
            entry.coverageStatus = entry.expectedForwardSubmeshCount > 0
                ? StaticDrawCoverageStatus::Invalid
                : StaticDrawCoverageStatus::None;
            return;
        }

        const std::vector<MATH::Mat4> nodeGlobals = BuildRenderModelNodeGlobals(renderModel);

        // 静的描画互換 cache は surface 契約を draw record に畳む。
        entry.records.reserve(renderModel.surfaces.size());
        for (size_t surfaceIndex = 0; surfaceIndex < renderModel.surfaces.size(); ++surfaceIndex) {
            const RenderSurfaceRecord& surface = renderModel.surfaces[surfaceIndex];
            ++entry.expectedForwardSubmeshCount;
            if (surface.skinningMode == RenderSurfaceSkinningMode::Skinned) {
                ++entry.skippedSkinnedSubmeshCount;
                ++entry.skippedUnsupportedSubmeshCount;
                continue;
            }

            StaticDrawRecord record{};
            record.objectId = object.desc.id;
            record.objectVersion = object.version;
            record.model = object.desc.model;
            record.renderModel = object.desc.renderModel;
            record.submeshIndex = ClampToUint32(surfaceIndex);
            record.nodeIndex = surface.nodeIndex;
            record.meshIndex = surface.meshIndex;
            record.primitiveIndex = surface.primitiveIndex;
            record.materialIndex = surface.materialIndex;
            record.objectWorldTransform = object.desc.worldTransform;
            record.drawTransform = object.desc.worldTransform;
            record.hasDrawWorldMatrix = ResolveRenderSurfaceDrawWorldMatrix(
                object.desc.worldTransform,
                surface,
                nodeGlobals,
                record.drawWorldMatrix);
            if (record.hasDrawWorldMatrix && surface.nodeIndex != kInvalidRenderModelIndex) {
                // static record は node 変換込みの最終行列を保持する。
                record.drawTransform.useExplicitMatrix = true;
                record.drawTransform.explicitMatrix = record.drawWorldMatrix;
            }
            record.worldBounds = ResolveRenderSurfaceWorldBounds(
                object.desc.worldBounds,
                surface,
                record.drawWorldMatrix,
                record.hasDrawWorldMatrix);
            record.castShadow = object.desc.castShadow;
            record.receiveShadow = object.desc.receiveShadow;
            record.materialOverride = object.desc.materialOverride;
            record.materialFxProfileId = object.desc.materialFxProfileId;
            record.postGroupMask = object.desc.postGroupMask;
            record.materialFxValuesInitialized = object.desc.materialFxValuesInitialized;
            CopyMaterialFxValues(object.desc, record);

            const StaticDrawRecordValidationResult validation = ValidateStaticDrawRecord(record);
            if (!validation.IsValid()) {
                ++entry.skippedInvalidRecordCount;
                if (validation.invalidBounds) {
                    ++entry.invalidRecordBoundsCount;
                }
                if (validation.missingDrawMatrix) {
                    ++entry.missingDrawMatrixCount;
                }
                if (validation.invalidPrimitiveIndex) {
                    ++entry.invalidPrimitiveIndexCount;
                }
                continue;
            }

            entry.records.push_back(std::move(record));
            ++entry.validForwardRecordCount;
        }

        if (entry.expectedForwardSubmeshCount == 0) {
            entry.coverageStatus = StaticDrawCoverageStatus::None;
        } else if (entry.validForwardRecordCount == entry.expectedForwardSubmeshCount) {
            entry.coverageStatus = StaticDrawCoverageStatus::Full;
        } else if (entry.validForwardRecordCount > 0) {
            entry.coverageStatus = StaticDrawCoverageStatus::Partial;
        } else {
            entry.coverageStatus = StaticDrawCoverageStatus::Invalid;
        }
    }

    void StaticDrawRecordCache::RebuildFlatRecordList() {
        records_.clear();
        uint64_t recordCount = 0;
        for (const uint64_t objectId : activeObjectIds_) {
            const auto found = entries_.find(objectId);
            if (found == entries_.end()) {
                continue;
            }
            const StaticDrawObjectEntry& entry = found->second;
            if (!entry.valid) {
                continue;
            }
            ++stats_.cachedObjectCount;
            stats_.skippedSkinnedSubmeshCount += entry.skippedSkinnedSubmeshCount;
            stats_.invalidRecordBoundsCount += entry.invalidRecordBoundsCount;
            stats_.missingDrawMatrixCount += entry.missingDrawMatrixCount;
            stats_.invalidPrimitiveIndexCount += entry.invalidPrimitiveIndexCount;
            stats_.validRecordCount += entry.validForwardRecordCount;
            if (entry.skippedAnimatedObject) {
                ++stats_.skippedAnimatedObjectCount;
            }
            if (entry.skippedDebugModeObject) {
                ++stats_.skippedDebugModeObjectCount;
            }
            stats_.expectedForwardSubmeshCount += entry.expectedForwardSubmeshCount;
            stats_.validForwardRecordCount += entry.validForwardRecordCount;

            switch (entry.coverageStatus) {
            case StaticDrawCoverageStatus::Full:
                ++stats_.fullCoverageObjectCount;
                recordCount += entry.records.size();
                break;
            case StaticDrawCoverageStatus::Partial:
                ++stats_.partialCoverageObjectCount;
                break;
            case StaticDrawCoverageStatus::Invalid:
                ++stats_.invalidCoverageObjectCount;
                break;
            case StaticDrawCoverageStatus::None:
            default:
                ++stats_.noCoverageObjectCount;
                break;
            }
        }
        records_.reserve(static_cast<size_t>(
            (std::min)(recordCount, static_cast<uint64_t>((std::numeric_limits<size_t>::max)()))));

        // SceneRenderCache の順序を保ったまま平坦化する。
        for (const uint64_t objectId : activeObjectIds_) {
            const auto found = entries_.find(objectId);
            if (found == entries_.end()) {
                continue;
            }
            const StaticDrawObjectEntry& entry = found->second;
            if (!entry.valid || entry.coverageStatus != StaticDrawCoverageStatus::Full) {
                continue;
            }
            records_.insert(records_.end(), entry.records.begin(), entry.records.end());
        }
        stats_.cachedRecordCount = ClampToUint32(records_.size());
    }

}
