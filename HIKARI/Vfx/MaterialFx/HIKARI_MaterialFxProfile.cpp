#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

#include <algorithm>
#include <fstream>
#include <unordered_map>

#include <json.hpp>

namespace HIKARI {

namespace {
    std::unordered_map<std::string, MaterialFxProfile> gProfileCache;
    MaterialFxProfileCacheStats gProfileCacheStats{};

    VFX::CompositeMode ParseComposite(const nlohmann::json& in, VFX::CompositeMode fallback) {
        const std::string value = in.is_string() ? in.get<std::string>() : std::string{};
        if (value == "Alpha") return VFX::CompositeMode::Alpha;
        if (value == "Additive") return VFX::CompositeMode::Additive;
        if (value == "Multiply") return VFX::CompositeMode::Multiply;
        if (value == "Screen") return VFX::CompositeMode::Screen;
        if (value == "Replace") return VFX::CompositeMode::Replace;
        return fallback;
    }

    VFX::ParamType ParseParamType(const nlohmann::json& in, VFX::ParamType fallback) {
        const std::string value = in.is_string() ? in.get<std::string>() : std::string{};
        if (value == "Float" || value == "float") return VFX::ParamType::Float;
        if (value == "Float2" || value == "float2") return VFX::ParamType::Float2;
        if (value == "Float3" || value == "float3") return VFX::ParamType::Float3;
        if (value == "Float4" || value == "float4") return VFX::ParamType::Float4;
        if (value == "Color" || value == "Color4" || value == "color4") return VFX::ParamType::Color4;
        if (value == "Color3" || value == "color3") return VFX::ParamType::Color3;
        if (value == "Toggle" || value == "toggle") return VFX::ParamType::Toggle;
        return fallback;
    }

    uint8_t ParseComponentChannel(const nlohmann::json& in) {
        const std::string value = in.is_string() ? in.get<std::string>() : std::string{};
        if (value.empty() || value == "x" || value == "xy" || value == "xyz" || value == "xyzw") return 0;
        if (value == "y" || value == "yz" || value == "yzw") return 1;
        if (value == "z" || value == "zw") return 2;
        if (value == "w") return 3;
        return 0;
    }

    size_t ParamComponentCount(VFX::ParamType type) {
        switch (type) {
        case VFX::ParamType::Float2: return 2;
        case VFX::ParamType::Float3:
        case VFX::ParamType::Color3: return 3;
        case VFX::ParamType::Float4:
        case VFX::ParamType::Color:
        case VFX::ParamType::Color4: return 4;
        case VFX::ParamType::Float:
        case VFX::ParamType::Toggle:
        default: return 1;
        }
    }

    void ParseFloatArray4(const nlohmann::json& in, float (&out)[4]) {
        if (in.is_number()) {
            out[0] = in.get<float>();
            return;
        }
        if (!in.is_array()) { return; }
        for (size_t i = 0; i < 4 && i < in.size(); ++i) {
            if (in[i].is_number()) {
                out[i] = in[i].get<float>();
            }
        }
    }

    VFX::ParamDesc ParseParamNode(const nlohmann::json& paramNode) {
        VFX::ParamDesc param{};
        param.key = paramNode.value("key", param.key);
        param.label = paramNode.value("label", paramNode.value("displayName", param.label));
        param.type = ParseParamType(paramNode.value("type", nlohmann::json{}), param.type);
        if (paramNode.contains("ref") && paramNode["ref"].is_object()) {
            const auto& ref = paramNode["ref"];
            param.ref.slot = static_cast<uint8_t>(ref.value("slot", static_cast<int>(param.ref.slot)));
            param.ref.channel = static_cast<uint8_t>(ref.value("channel", static_cast<int>(param.ref.channel)));
        } else {
            param.ref.slot = static_cast<uint8_t>(paramNode.value("slot", static_cast<int>(param.ref.slot)));
            param.ref.channel = ParseComponentChannel(paramNode.value("component", nlohmann::json{}));
        }

        ParseFloatArray4(paramNode.value("defaultValues", paramNode.value("default", nlohmann::json{})), param.defaultValues);
        ParseFloatArray4(paramNode.value("minValues", paramNode.value("min", nlohmann::json{})), param.minValues);
        ParseFloatArray4(paramNode.value("maxValues", paramNode.value("max", nlohmann::json{})), param.maxValues);
        param.speed = paramNode.value("speed", param.speed);
        return param;
    }

    bool ParseProfileNode(const nlohmann::json& node, const std::string& targetId, MaterialFxProfile& outProfile) {
        if (!node.is_object()) {
            return false;
        }
        if (node.value("id", std::string{}) != targetId) {
            return false;
        }

        outProfile = MaterialFxProfile{};
        outProfile.id = node.value("id", targetId);
        outProfile.displayName = node.value("displayName", outProfile.displayName);
        outProfile.shaderProfileId = node.value("shaderProfileId", outProfile.shaderProfileId);
        outProfile.vertexShaderId = node.value("vertexShaderId", outProfile.vertexShaderId);
        outProfile.pixelShaderId = node.value("pixelShaderId", outProfile.pixelShaderId);
        outProfile.featureBits = node.value("featureBits", outProfile.featureBits);
        outProfile.depthTest = node.value("depthTest", outProfile.depthTest);
        outProfile.depthWrite = node.value("depthWrite", outProfile.depthWrite);
        outProfile.doubleSided = node.value("doubleSided", outProfile.doubleSided);
        outProfile.composite = ParseComposite(node.value("composite", nlohmann::json{}), outProfile.composite);

        outProfile.params.clear();
        const nlohmann::json* paramsNode = nullptr;
        if (node.contains("params") && node["params"].is_array()) {
            paramsNode = &node["params"];
        } else if (node.contains("parameters") && node["parameters"].is_array()) {
            paramsNode = &node["parameters"];
        }
        if (paramsNode != nullptr) {
            for (const auto& paramNode : *paramsNode) {
                if (!paramNode.is_object()) {
                    continue;
                }
                outProfile.params.push_back(ParseParamNode(paramNode));
            }
        }

        outProfile.values.fill(DirectX::XMFLOAT4{});
        for (const VFX::ParamDesc& param : outProfile.params) {
            const size_t slot = static_cast<size_t>(param.ref.slot);
            const size_t channel = static_cast<size_t>(param.ref.channel);
            if (slot >= outProfile.values.size() || channel >= 4u) {
                continue;
            }
            float* dst = &outProfile.values[slot].x;
            const size_t writeCount = std::min<size_t>(4u - channel, ParamComponentCount(param.type));
            for (size_t i = 0; i < writeCount; ++i) {
                dst[channel + i] = param.defaultValues[i];
            }
        }

        return true;
    }
}

bool MaterialFxProfile::LoadFromJson(const std::string& path) {
    if (id.empty()) {
        return false;
    }

    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        return false;
    }

    nlohmann::json root = nlohmann::json::parse(ifs, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return false;
    }

    if (root.contains("profiles") && root["profiles"].is_array()) {
        for (const auto& profileNode : root["profiles"]) {
            if (ParseProfileNode(profileNode, id, *this)) {
                return true;
            }
        }
    }

    return ParseProfileNode(root, id, *this);
}

bool MaterialFxProfile::LoadById(const std::string& profileId, MaterialFxProfile& out) {
    if (profileId.empty()) {
        return false;
    }

    const auto found = gProfileCache.find(profileId);
    if (found != gProfileCache.end()) {
        ++gProfileCacheStats.hitCount;
        out = found->second;
        return true;
    }

    ++gProfileCacheStats.missCount;

    MaterialFxProfile loaded{};
    loaded.id = profileId;
    if (!loaded.LoadFromJson("Data/material_fx_profiles.json")) {
        ++gProfileCacheStats.failCount;
        return false;
    }

    const auto inserted = gProfileCache.emplace(profileId, std::move(loaded));
    out = inserted.first->second;
    return true;
}

void MaterialFxProfile::ClearCache() {
    gProfileCache.clear();
    gProfileCacheStats = {};
}

MaterialFxProfileCacheStats MaterialFxProfile::GetCacheStats() {
    return gProfileCacheStats;
}

void MaterialFxProfile::CopyValuesTo(DirectX::XMFLOAT4(&dst)[4]) const {
    const size_t count = std::min(values.size(), std::size(dst));
    for (size_t i = 0; i < count; ++i) {
        dst[i] = values[i];
    }
}

} // namespace HIKARI
