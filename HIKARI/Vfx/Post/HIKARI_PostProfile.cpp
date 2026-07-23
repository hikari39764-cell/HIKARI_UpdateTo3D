#include "Vfx/Post/HIKARI_PostProfile.h"

#include <algorithm>
#include <iterator>

#include <json.hpp>
#include "Core/Serialization/Json/HIKARI_JsonFile.h"

namespace HIKARI {

    namespace {
        VFX::FxDomain ParseDomain(const nlohmann::json& in, VFX::FxDomain fallback) {
            const std::string value = in.is_string() ? in.get<std::string>() : std::string{};
            if (value == "GlobalPost") return VFX::FxDomain::GlobalPost;
            if (value == "ScopedPost") return VFX::FxDomain::ScopedPost;
            return fallback;
        }

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
    }

    bool PostProfile::LoadById(const std::string& profileId, PostProfile& out) {
        if (profileId.empty()) {
            return false;
        }
        const std::string path = std::string("Data/post_profiles/") + profileId + ".json";
        return out.LoadFromJson(path);
    }

    bool PostProfile::LoadFromJson(const std::string& path) {
        nlohmann::json root{};
        if (!SERIALIZATION::JSON::ReadJsonFile(
                std::filesystem::path(path),
                root) ||
            !root.is_object()) {
            return false;
        }

        id = root.value("id", id);
        displayName = root.value("displayName", displayName);
        domain = ParseDomain(root.value("domain", nlohmann::json{}), domain);

        passes.clear();
        if (root.contains("passes") && root["passes"].is_array()) {
            for (const auto& passNode : root["passes"]) {
                if (!passNode.is_object()) {
                    continue;
                }
                VFX::PassDescriptor pass{};
                pass.passName = passNode.value("passName", pass.passName);
                pass.shaderId = passNode.value("shaderId", pass.shaderId);
                pass.composite = ParseComposite(passNode.value("composite", nlohmann::json{}), pass.composite);
                pass.depthTest = passNode.value("depthTest", pass.depthTest);
                pass.depthWrite = passNode.value("depthWrite", pass.depthWrite);
                pass.doubleSided = passNode.value("doubleSided", pass.doubleSided);
                passes.push_back(std::move(pass));
            }
        }

        params.clear();
        if (root.contains("params") && root["params"].is_array()) {
            for (const auto& paramNode : root["params"]) {
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
                params.push_back(std::move(param));
            }
        }

        ResetValuesFromDefaults();
        return true;
    }

    void PostProfile::ResetValuesFromDefaults() {
        values.fill(DirectX::XMFLOAT4{});
        for (const VFX::ParamDesc& param : params) {
            const size_t slot = static_cast<size_t>(param.ref.slot);
            const size_t channel = static_cast<size_t>(param.ref.channel);
            if (slot >= values.size() || channel >= 4u) {
                continue;
            }
            float* dst = &values[slot].x;
            const size_t writeCount = std::min<size_t>(4u - channel, 4u);
            for (size_t i = 0; i < writeCount; ++i) {
                dst[channel + i] = param.defaultValues[i];
            }
        }
    }

    void PostProfile::ApplyToCommonParams(POST::CommonParams& out) const {
        const size_t count = std::min(values.size(), std::size(out.user));
        for (size_t i = 0; i < count; ++i) {
            out.user[i] = values[i];
        }
    }

    void PostProfile::CopyValuesTo(DirectX::XMFLOAT4(&dst)[16]) const {
        const size_t count = std::min(values.size(), std::size(dst));
        for (size_t i = 0; i < count; ++i) {
            dst[i] = values[i];
        }
    }

} // namespace HIKARI
