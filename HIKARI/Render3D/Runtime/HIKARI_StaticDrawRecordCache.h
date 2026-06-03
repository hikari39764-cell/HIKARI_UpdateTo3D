#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <DirectXMath.h>
#include <Vfx/Common/HIKARI_FxTypes.h>

#include "Render3D/Runtime/HIKARI_SceneRenderCache.h"

namespace HIKARI::RENDER3D::RUNTIME {

    enum class StaticDrawCoverageStatus {
        None,
        Full,
        Partial,
        Invalid,
    };

    struct StaticDrawRecord {
        SceneRenderObjectId objectId{};
        uint64_t objectVersion = 0;

        const ModelAsset* model = nullptr;
        const RenderModelAsset* renderModel = nullptr;

        uint32_t submeshIndex = 0;
        uint32_t nodeIndex = 0;
        uint32_t meshIndex = 0;
        uint32_t primitiveIndex = 0;
        uint32_t materialIndex = 0;

        Transform3D objectWorldTransform{};
        Transform3D drawTransform{};

        MATH::Mat4 drawWorldMatrix{};
        bool hasDrawWorldMatrix = false;

        Bounds worldBounds{};

        bool castShadow = true;
        bool receiveShadow = true;

        const Material* materialOverride = nullptr;

        std::string materialFxProfileId{};
        uint32_t postGroupMask = 0;

        DirectX::XMFLOAT4 materialFxParamValues[VFX::kMaterialFxUserCount]{};
        bool materialFxValuesInitialized = false;
    };

    struct StaticDrawRecordValidationResult {
        bool invalidObject = false;
        bool invalidRenderModel = false;
        bool invalidBounds = false;
        bool missingDrawMatrix = false;
        bool invalidPrimitiveIndex = false;

        bool IsValid() const {
            return
                !invalidObject &&
                !invalidRenderModel &&
                !invalidBounds &&
                !missingDrawMatrix &&
                !invalidPrimitiveIndex;
        }
    };

    struct StaticDrawObjectEntry {
        SceneRenderObjectId objectId{};
        uint64_t sourceVersion = 0;

        bool valid = false;
        uint32_t expectedForwardSubmeshCount = 0;
        uint32_t validForwardRecordCount = 0;

        uint32_t skippedSkinnedSubmeshCount = 0;
        uint32_t skippedInvalidRecordCount = 0;
        uint32_t skippedUnsupportedSubmeshCount = 0;

        uint32_t invalidRecordBoundsCount = 0;
        uint32_t missingDrawMatrixCount = 0;
        uint32_t invalidPrimitiveIndexCount = 0;
        bool skippedAnimatedObject = false;
        bool skippedDebugModeObject = false;

        StaticDrawCoverageStatus coverageStatus = StaticDrawCoverageStatus::None;
        std::vector<StaticDrawRecord> records{};
    };

    class StaticDrawRecordCache {
    public:
        struct Stats {
            uint32_t staticObjectCount = 0;
            uint32_t cachedObjectCount = 0;
            uint32_t cachedRecordCount = 0;

            uint32_t rebuiltObjectCount = 0;
            uint32_t reusedObjectCount = 0;
            uint32_t removedObjectCount = 0;

            uint32_t skippedDynamicObjectCount = 0;
            uint32_t skippedInvisibleObjectCount = 0;
            uint32_t skippedInvalidObjectCount = 0;
            uint32_t skippedInvalidRenderModelCount = 0;
            uint32_t skippedSkinnedSubmeshCount = 0;

            uint32_t invalidRecordBoundsCount = 0;
            uint32_t missingDrawMatrixCount = 0;
            uint32_t invalidPrimitiveIndexCount = 0;
            uint32_t validRecordCount = 0;
            uint32_t skippedAnimatedObjectCount = 0;
            uint32_t skippedDebugModeObjectCount = 0;

            uint32_t fullCoverageObjectCount = 0;
            uint32_t partialCoverageObjectCount = 0;
            uint32_t noCoverageObjectCount = 0;
            uint32_t invalidCoverageObjectCount = 0;

            uint32_t expectedForwardSubmeshCount = 0;
            uint32_t validForwardRecordCount = 0;
        };

        void Clear();
        void SyncFromSceneRenderCache(const SceneRenderCache& sceneCache);

        const std::vector<StaticDrawRecord>& GetRecords() const;
        const Stats& GetStats() const;
        bool HasValidRecordsForObject(SceneRenderObjectId objectId) const;
        bool HasFullForwardCoverageForObject(SceneRenderObjectId objectId) const;
        StaticDrawCoverageStatus GetCoverageStatusForObject(SceneRenderObjectId objectId) const;

    private:
        StaticDrawRecordValidationResult ValidateStaticDrawRecord(const StaticDrawRecord& record) const;
        void RebuildObjectRecords(const SceneRenderObject& object, StaticDrawObjectEntry& entry);
        void RebuildFlatRecordList();

        std::unordered_map<uint64_t, StaticDrawObjectEntry> entries_{};
        std::vector<uint64_t> activeObjectIds_{};
        std::vector<StaticDrawRecord> records_{};
        Stats stats_{};
    };

}
