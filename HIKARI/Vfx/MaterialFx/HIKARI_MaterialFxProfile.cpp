#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"

#include <algorithm>
#include <fstream>
#include <cstdio>

#if defined(_WIN32)
#include <windows.h>
#endif

#include <json.hpp>

namespace HIKARI {

namespace {
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
        if (value == "Float") return VFX::ParamType::Float;
        if (value == "Float2") return VFX::ParamType::Float2;
        if (value == "Float3") return VFX::ParamType::Float3;
        if (value == "Float4") return VFX::ParamType::Float4;
        if (value == "Color") return VFX::ParamType::Color;
        if (value == "Toggle") return VFX::ParamType::Toggle;
        return fallback;
    }

    void ParseFloatArray4(const nlohmann::json& in, float (&out)[4]) {
        if (!in.is_array()) {
            return;
        }
        for (size_t i = 0; i < 4 && i < in.size(); ++i) {
            if (in[i].is_number()) {
                out[i] = in[i].get<float>();
            }
        }
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
        outProfile.featureBits = node.value("featureBits", outProfile.featureBits);
        outProfile.depthTest = node.value("depthTest", outProfile.depthTest);
        outProfile.depthWrite = node.value("depthWrite", outProfile.depthWrite);
        outProfile.doubleSided = node.value("doubleSided", outProfile.doubleSided);
        outProfile.composite = ParseComposite(node.value("composite", nlohmann::json{}), outProfile.composite);

        outProfile.params.clear();
        if (node.contains("params") && node["params"].is_array()) {
            for (const auto& paramNode : node["params"]) {
                if (!paramNode.is_object()) {
                    continue;
                }

                VFX::ParamDesc param{};
                param.key = paramNode.value("key", param.key);
                param.label = paramNode.value("label", param.label);
                param.type = ParseParamType(paramNode.value("type", nlohmann::json{}), param.type);
                if (paramNode.contains("ref") && paramNode["ref"].is_object()) {
                    const auto& ref = paramNode["ref"];
                    param.ref.slot = static_cast<uint8_t>(ref.value("slot", static_cast<int>(param.ref.slot)));
                    param.ref.channel = static_cast<uint8_t>(ref.value("channel", static_cast<int>(param.ref.channel)));
                }
                ParseFloatArray4(paramNode.value("defaultValues", nlohmann::json{}), param.defaultValues);
                ParseFloatArray4(paramNode.value("minValues", nlohmann::json{}), param.minValues);
                ParseFloatArray4(paramNode.value("maxValues", nlohmann::json{}), param.maxValues);
                param.speed = paramNode.value("speed", param.speed);
                outProfile.params.push_back(std::move(param));
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
            const size_t writeCount = std::min<size_t>(4u - channel, 4u);
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
#if defined(_DEBUG)
        char msg[512]{};
        std::snprintf(msg, sizeof(msg), "[MaterialFxProfile] LoadFromJson open failed path=%s profileId=%s\n", path.c_str(), id.c_str());
        OutputDebugStringA(msg);
#endif
        return false;
    }

    nlohmann::json root = nlohmann::json::parse(ifs, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
#if defined(_DEBUG)
        char msg[512]{};
        std::snprintf(msg, sizeof(msg), "[MaterialFxProfile] LoadFromJson parse failed path=%s profileId=%s\n", path.c_str(), id.c_str());
        OutputDebugStringA(msg);
#endif
        return false;
    }

    if (root.contains("profiles") && root["profiles"].is_array()) {
        for (const auto& profileNode : root["profiles"]) {
            if (ParseProfileNode(profileNode, id, *this)) {
                #if defined(_DEBUG)
                char msg[768]{};
                std::snprintf(msg, sizeof(msg), "[MaterialFxProfile] LoadFromJson success path=%s profileId=%s shaderProfileId=%s composite=%d\n", path.c_str(), id.c_str(), shaderProfileId.c_str(), static_cast<int>(composite));
                OutputDebugStringA(msg);
                #endif
                return true;
            }
        }
    }
    const bool ok = ParseProfileNode(root, id, *this);
#if defined(_DEBUG)
    char msg[768]{};
    std::snprintf(msg, sizeof(msg), "[MaterialFxProfile] LoadFromJson %s path=%s profileId=%s shaderProfileId=%s composite=%d\n", ok ? "success" : "not_found", path.c_str(), id.c_str(), shaderProfileId.c_str(), static_cast<int>(composite));
    OutputDebugStringA(msg);
#endif
    return ok;
}

bool MaterialFxProfile::LoadById(const std::string& profileId, MaterialFxProfile& out) {
    if (profileId.empty()) {
        return false;
    }
    out.id = profileId;
    const bool ok = out.LoadFromJson("Data/material_fx_profiles.json");
#if defined(_DEBUG)
    char msg[768]{};
    std::snprintf(msg, sizeof(msg), "[MaterialFxProfile] LoadById profileId=%s path=%s success=%s shaderProfileId=%s composite=%d\n", profileId.c_str(), "Data/material_fx_profiles.json", ok ? "true" : "false", out.shaderProfileId.c_str(), static_cast<int>(out.composite));
    OutputDebugStringA(msg);
#endif
    return ok;
}

void MaterialFxProfile::CopyValuesTo(DirectX::XMFLOAT4(&dst)[4]) const {
    const size_t count = std::min(values.size(), std::size(dst));
    for (size_t i = 0; i < count; ++i) {
        dst[i] = values[i];
    }
}

} // namespace HIKARI
