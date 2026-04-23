#pragma once

#include <string>
#include <cstdint>
#include <DirectXMath.h>

#include "Assets/HIKARI_Assets.h"
#include "Render3D/HIKARI_Transform3D.h"

namespace HIKARI {
    class ModelAsset;
}

namespace HIKARI::MODELR {

    void Submit(const HIKARI::ModelAsset& legacyModelAsset,
        const Transform3D& transform,
        const std::string& materialFxProfileId,
        uint32_t postGroupMask,
        const DirectX::XMFLOAT4(&materialFxParamValues)[4],
        bool materialFxValuesInitialized);

} // namespace HIKARI::MODELR
