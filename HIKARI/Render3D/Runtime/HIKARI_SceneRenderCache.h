#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include <DirectXMath.h>
#include <Vfx/Common/HIKARI_FxTypes.h>

#include "Render3D/Core/HIKARI_ModelAsset.h"
#include "Render3D/Runtime/HIKARI_RenderModelAsset.h"

namespace HIKARI {
    class Material;
}

namespace HIKARI::RENDER3D::RUNTIME {

    struct SceneRenderObjectId {
        uint64_t value = 0;

        bool IsValid() const {
            return value != 0;
        }

        bool operator==(const SceneRenderObjectId& rhs) const {
            return value == rhs.value;
        }
    };

    struct SceneRenderObjectDesc {
        SceneRenderObjectId id{};

        const ModelAsset* model = nullptr;
        const RenderModelAsset* renderModel = nullptr;

        Transform3D worldTransform{};

        Bounds localBounds{};
        Bounds worldBounds{};

        bool visible = true;
        bool isStatic = false;
        bool castShadow = true;
        bool receiveShadow = true;
        bool hasRuntimeAnimation = false;
        bool hasSpecialRenderDebug = false;
        bool allowStaticCachedForward = true;

        const Material* materialOverride = nullptr;

        std::string materialFxProfileId{};
        uint32_t postGroupMask = 0;

        DirectX::XMFLOAT4 materialFxParamValues[VFX::kMaterialFxUserCount]{};
        bool materialFxValuesInitialized = false;
    };

    struct SceneRenderObject {
        SceneRenderObjectDesc desc{};

        bool valid = false;
        bool dirty = true;
        bool transformDirty = true;
        bool materialDirty = true;
        bool modelDirty = true;

        uint64_t lastTouchedFrame = 0;
        uint64_t version = 0;
    };

    class SceneRenderCache {
    public:
        struct Stats {
            uint32_t renderObjectCount = 0;
            uint32_t visibleObjectCount = 0;
            uint32_t hiddenObjectCount = 0;

            uint32_t dirtyObjectCount = 0;
            uint32_t staticObjectCount = 0;
            uint32_t dynamicObjectCount = 0;

            uint32_t insertedCount = 0;
            uint32_t updatedCount = 0;
            uint32_t removedCount = 0;
            uint32_t invalidDescCount = 0;

            uint32_t renderModelValidCount = 0;
            uint32_t renderModelInvalidCount = 0;
        };

        void Clear();

        void BeginSync(uint64_t frameIndex);
        void Upsert(const SceneRenderObjectDesc& desc);
        void EndSync();

        void Remove(SceneRenderObjectId id);
        void OnSceneLoaded();
        void OnSceneUnloaded();
        void MarkAllDirty();
        void PreRenderSync();

        const SceneRenderObject* Find(SceneRenderObjectId id) const;
        const std::vector<SceneRenderObject>& GetObjects() const;
        const Stats& GetStats() const;

    private:
        void RemoveAt(size_t index);
        void RebuildIndex();
        void RefreshStats();

        std::vector<SceneRenderObject> objects_{};
        std::unordered_map<uint64_t, size_t> indexById_{};

        Stats frameStats_{};
        uint64_t currentSyncFrame_ = 0;
    };

}
