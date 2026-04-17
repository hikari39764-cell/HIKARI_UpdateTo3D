#pragma once

#include <string>
#include <vector>

#include "HIKARI_FxTypes.h"

namespace HIKARI {

class PostProfile {
public:
    std::string id;
    std::string displayName;
    VFX::FxDomain domain = VFX::FxDomain::GlobalPost;
    std::vector<VFX::PassDescriptor> passes;
    std::vector<VFX::ParamDesc> params;

    bool LoadFromJson(const std::string& path);
};

} // namespace HIKARI
