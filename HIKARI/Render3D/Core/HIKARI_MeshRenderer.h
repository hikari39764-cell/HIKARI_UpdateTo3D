#pragma once
#include <array>
#include <vector>
#include <string>
#include <cstdint>
#include <DirectXMath.h>
#include "Assets/HIKARI_Assets.h"
#include "Render3D/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_SceneEnvironment.h"

namespace HIKARI::MESHRENDERER {
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
    SubmissionRendererDebugOptions& GetDebugOptions();

} // namespace HIKARI::MESHRENDERER
