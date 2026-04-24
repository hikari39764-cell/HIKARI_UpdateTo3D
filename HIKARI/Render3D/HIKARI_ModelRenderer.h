#pragma once

#include <string>
#include <cstdint>
#include <DirectXMath.h>

#include "Assets/HIKARI_Assets.h"
#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI::MODELR {

    void SubmitModelComponent(
        ASSET::AssetRegistry& registry,
        ASSET::AssetHandle<ASSET::ModelAsset> model,
        const Transform3D& world,
        bool visible,
        bool castShadow,
        bool receiveShadow,
        uint32_t renderLayerMask,
        const std::string& materialFxProfileId,
        uint32_t postGroupMask,
        const DirectX::XMFLOAT4(&materialFxParamValues)[4],
        bool materialFxValuesInitialized);

} // namespace HIKARI::MODELR
