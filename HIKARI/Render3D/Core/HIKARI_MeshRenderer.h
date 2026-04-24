#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include <DirectXMath.h>
#include "Assets/HIKARI_Assets.h"
#include "Render3D/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_SceneEnvironment.h"
#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI::MESHRENDERER {
    struct StaticModelSubmission {
        ASSET::AssetHandle<ASSET::ModelAsset> model{};
        Transform3D world{};
        bool visible = true;
        bool castShadow = true;
        bool receiveShadow = true;
        uint32_t renderLayerMask = 0xFFFFFFFFu;
        uint32_t postGroupMask = 0;
        std::string materialFxProfileId{};
        DirectX::XMFLOAT4 materialFxUser[4]{};
        bool materialFxValuesInitialized = false;
    };

    void Reset();
    void SubmitStaticModel(ASSET::AssetRegistry& registry, const StaticModelSubmission& submission);
    void RenderAll(const Camera3D& camera, const SceneEnvironment& environment);

} // namespace HIKARI::MESHRENDERER
