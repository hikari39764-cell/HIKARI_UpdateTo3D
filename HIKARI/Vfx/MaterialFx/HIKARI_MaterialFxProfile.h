#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include <DirectXMath.h>

#include "Vfx/Common/HIKARI_FxTypes.h"

namespace HIKARI {

struct MaterialFxProfileCacheStats {
    size_t hitCount = 0;
    size_t missCount = 0;
    size_t failCount = 0;
};

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

    static bool LoadById(const std::string& profileId, MaterialFxProfile& out);
    static void ClearCache();
    static MaterialFxProfileCacheStats GetCacheStats();
    bool LoadFromJson(const std::string& path);
    void CopyValuesTo(DirectX::XMFLOAT4 (&dst)[4]) const;
};

} // namespace HIKARI
