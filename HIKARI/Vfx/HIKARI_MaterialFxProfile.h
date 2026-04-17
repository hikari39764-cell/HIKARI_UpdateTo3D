#pragma once

#include <array>
#include <string>
#include <vector>

#include <DirectXMath.h>

#include "HIKARI_FxTypes.h"

namespace HIKARI {

class MaterialFxProfile {
public:
    std::string id;
    std::string displayName;
    std::string shaderProfileId;
    uint32_t featureBits = 0;
    bool depthTest = true;
    bool depthWrite = true;
    bool doubleSided = false;
    VFX::CompositeMode composite = VFX::CompositeMode::Alpha;
    std::vector<VFX::ParamDesc> params;
    std::array<DirectX::XMFLOAT4, 4> values{};

    bool LoadFromJson(const std::string& path);
};

} // namespace HIKARI
