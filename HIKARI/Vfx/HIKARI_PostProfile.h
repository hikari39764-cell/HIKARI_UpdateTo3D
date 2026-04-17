#pragma once

#include <string>
#include <array>
#include <vector>
#include <DirectXMath.h>

#include "HIKARI_FxTypes.h"
#include "HIKARI_PostCommon.h"

namespace HIKARI {

class PostProfile {
public:
    std::string id;
    std::string displayName;
    VFX::FxDomain domain = VFX::FxDomain::GlobalPost;
    std::vector<VFX::PassDescriptor> passes;
    std::vector<VFX::ParamDesc> params;
    std::array<DirectX::XMFLOAT4, 16> values{};

    bool LoadFromJson(const std::string& path);
    void ResetValuesFromDefaults();
    void ApplyToCommonParams(POST::CommonParams& out) const;
};

} // namespace HIKARI
