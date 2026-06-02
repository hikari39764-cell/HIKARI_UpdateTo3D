#include "Render3D/Runtime/HIKARI_StaticDrawRecordCache.h"

#include <algorithm>
#include <limits>
#include <unordered_set>
#include <utility>

#include "Render3D/Core/HIKARI_BoundsUtils.h"

namespace HIKARI::RENDER3D::RUNTIME {

    namespace {
        uint32_t ClampToUint32(size_t value) {
            return static_cast<uint32_t>((std::min)(value, static_cast<size_t>((std::numeric_limits<uint32_t>::max)())));
        }

        void CopyMaterialFxValues(const SceneRenderObjectDesc& desc, StaticDrawRecord& record) {
            for (int i = 0; i < VFX::kMaterialFxUserCount; ++i) {
                record.materialFxParamValues[i] = desc.materialFxParamValues[i];
            }
        }

        MATH::Mat4 BuildNodeGlobalRecursive(
            const RenderModelAsset& renderModel,
            size_t nodeIndex,
            std::vector<MATH::Mat4>& globals,
            std::vector<uint8_t>& visited) {

            if (nodeIndex >= renderModel.nodes.size()) {
                return MATH::Mat4::Identity();
            }
            if (visited[nodeIndex]) {
                return globals[nodeIndex];
            }

            const RenderModelNodeRecord& node = renderModel.nodes[nodeIndex];
            MATH::Mat4 parent = MATH::Mat4::Identity();
            if (node.parentIndex >= 0 && node.parentIndex < static_cast<int>(renderModel.nodes.size())) {
                parent = BuildNodeGlobalRecursive(renderModel, static_cast<size_t>(node.parentIndex), globals, visited);
            }

            globals[nodeIndex] = parent * node.localMatrix;
            visited[nodeIndex] = 1u;
            return globals[nodeIndex];
        }

        std::vector<MATH::Mat4> BuildNodeGlobals(const RenderModelAsset& renderModel) {
            std::vector<MATH::Mat4> globals(renderModel.nodes.size(), MATH::Mat4::Identity());
            std::vector<uint8_t> visited(renderModel.nodes.size(), 0u);
            for (size_t nodeIndex = 0; nodeIndex < renderModel.nodes.size(); ++nodeIndex) {
                (void)BuildNodeGlobalRecursive(renderModel, nodeIndex, globals, visited);
            }
            return globals;
        }

        Bounds ResolveRecordWorldBounds(
            const SceneRenderObject& object,
            const RenderSubmeshRecord& submesh,
            const std::vector<MATH::Mat4>& nodeGlobals) {

            if (!BOUNDS::IsUsable(submesh.localBounds)) {
                return object.desc.worldBounds;
            }

            MATH::Mat4 localToWorld = object.desc.worldTransform.GetWorldMatrix();
            if (submesh.nodeIndex != kInvalidRenderModelIndex &&
                submesh.nodeIndex < nodeGlobals.size()) {
                localToWorld = localToWorld * nodeGlobals[submesh.nodeIndex];
            }

            const Bounds worldBounds = BOUNDS::TransformBounds(submesh.localBounds, localToWorld);
            return BOUNDS::IsUsable(worldBounds) ? worldBounds : object.desc.worldBounds;
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
                stats_.skippedSkinnedSubmeshCount += entry.skippedSkinnedSubmeshCount;
                continue;
            }

            RebuildObjectRecords(object, entry);
            stats_.skippedSkinnedSubmeshCount += entry.skippedSkinnedSubmeshCount;
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

    void StaticDrawRecordCache::RebuildObjectRecords(const SceneRenderObject& object, StaticDrawObjectEntry& entry) {
        entry.objectId = object.desc.id;
        entry.sourceVersion = object.version;
        entry.valid = true;
        entry.skippedSkinnedSubmeshCount = 0;
        entry.records.clear();

        const RenderModelAsset& renderModel = *object.desc.renderModel;
        const std::vector<MATH::Mat4> nodeGlobals = BuildNodeGlobals(renderModel);

        // 静的描画だけを次段階用に平坦化する。
        entry.records.reserve(renderModel.submeshes.size());
        for (size_t submeshIndex = 0; submeshIndex < renderModel.submeshes.size(); ++submeshIndex) {
            const RenderSubmeshRecord& submesh = renderModel.submeshes[submeshIndex];
            if (submesh.skinningMode == RenderSubmeshSkinningMode::Skinned) {
                ++entry.skippedSkinnedSubmeshCount;
                continue;
            }

            StaticDrawRecord record{};
            record.objectId = object.desc.id;
            record.objectVersion = object.version;
            record.model = object.desc.model;
            record.renderModel = object.desc.renderModel;
            record.submeshIndex = ClampToUint32(submeshIndex);
            record.nodeIndex = submesh.nodeIndex;
            record.meshIndex = submesh.meshIndex;
            record.primitiveIndex = submesh.primitiveIndex;
            record.materialIndex = submesh.materialIndex;
            record.worldTransform = object.desc.worldTransform;
            record.worldBounds = ResolveRecordWorldBounds(object, submesh, nodeGlobals);
            record.castShadow = object.desc.castShadow;
            record.receiveShadow = object.desc.receiveShadow;
            record.materialOverride = object.desc.materialOverride;
            record.materialFxProfileId = object.desc.materialFxProfileId;
            record.postGroupMask = object.desc.postGroupMask;
            record.materialFxValuesInitialized = object.desc.materialFxValuesInitialized;
            CopyMaterialFxValues(object.desc, record);

            entry.records.push_back(std::move(record));
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
            recordCount += entry.records.size();
        }
        records_.reserve(static_cast<size_t>((std::min)(recordCount, static_cast<uint64_t>((std::numeric_limits<size_t>::max)()))));

        // SceneRenderCache の順序を保ったまま平坦化する。
        for (const uint64_t objectId : activeObjectIds_) {
            const auto found = entries_.find(objectId);
            if (found == entries_.end()) {
                continue;
            }
            const StaticDrawObjectEntry& entry = found->second;
            if (!entry.valid) {
                continue;
            }
            records_.insert(records_.end(), entry.records.begin(), entry.records.end());
            ++stats_.cachedObjectCount;
        }
        stats_.cachedRecordCount = ClampToUint32(records_.size());
    }

}
