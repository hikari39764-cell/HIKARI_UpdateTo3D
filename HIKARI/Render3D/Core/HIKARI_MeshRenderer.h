#pragma once
#include <array>
#include <vector>
#include <string>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <DirectXMath.h>
#include "Assets/HIKARI_Assets.h"
#include "Render3D/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_SceneEnvironment.h"

namespace HIKARI::MESHRENDERER {
    struct ObjectFxBucketKey {
        uint32_t postGroupMask = 0;
        std::string materialFxProfileId{};

        bool operator==(const ObjectFxBucketKey& rhs) const {
            return postGroupMask == rhs.postGroupMask &&
                materialFxProfileId == rhs.materialFxProfileId;
        }
    };

    struct ObjectFxBucketKeyHasher {
        size_t operator()(const ObjectFxBucketKey& key) const noexcept {
            size_t seed = std::hash<uint32_t>{}(key.postGroupMask);
            seed ^= std::hash<std::string>{}(key.materialFxProfileId) + 0x9e3779b9u + (seed << 6) + (seed >> 2);
            return seed;
        }
    };

    struct StaticModelDrawItem {
        ASSET::AssetRegistry* registry = nullptr;
        uint32_t gpuMeshId = 0;
        ASSET::AssetHandle<ASSET::MaterialAsset> material{};
        MATH::Mat4 world{};
        MATH::Mat4 normalMatrix{};
        uint32_t renderLayerMask = 0xFFFFFFFFu;
        uint32_t postGroupMask = 0;
        bool castShadow = true;
        bool receiveShadow = true;
        std::string materialFxProfileId{};
        std::array<DirectX::XMFLOAT4, 4> materialFxUser{};
        bool materialFxValuesInitialized = false;
    };

    struct SubmissionRendererDebugOptions {
        bool rotateLight = false;
        bool useNormal = true;
        bool useEmissive = true;
    };

    void Reset();
    void SubmitStaticDrawItem(const StaticModelDrawItem& item);
    void RenderAll(const Camera3D& camera, const SceneEnvironment& environment);

    const std::vector<StaticModelDrawItem>& GetSubmittedDrawItems();
    size_t GetObjectFxBucketCount();
    const char* ResolveDrawItemBucketTag(const StaticModelDrawItem& item);
    SubmissionRendererDebugOptions& GetDebugOptions();

} // namespace HIKARI::MESHRENDERER
