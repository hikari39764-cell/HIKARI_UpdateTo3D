#include "HIKARI_ModelComponent.h"

#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"
#include "Vfx/MaterialFx/HIKARI_MaterialFxProfile.h"
#include "Vfx/Common/HIKARI_FxTypes.h"
#include <cstring>
#include <algorithm>
#include <utility>

#if defined(_DEBUG)
#include "imgui.h"
#endif

namespace HIKARI {

    namespace {
        bool ResolveParamRef(const MaterialFxProfile& profile, const std::string& key, VFX::ParamChannelRef& outRef, VFX::ParamType* outType = nullptr) {
            for (const VFX::ParamDesc& param : profile.params) {
                if (param.key == key) {
                    outRef = param.ref;
                    if (outType) {
                        *outType = param.type;
                    }
                    return true;
                }
            }
            return false;
        }

        bool EnsureMaterialFxValuesReady(const std::string& profileId, DirectX::XMFLOAT4(&values)[4], bool& initialized) {
            if (initialized) {
                return true;
            }
            if (profileId.empty()) {
                return false;
            }
            MaterialFxProfile profile{};
            if (!MaterialFxProfile::LoadById(profileId, profile)) {
                return false;
            }
            profile.CopyValuesTo(values);
            initialized = true;
            return true;
        }

#if defined(_DEBUG)
        bool DrawParamControl(const VFX::ParamDesc& param, DirectX::XMFLOAT4& slotValue) {
            float value[4] = { slotValue.x, slotValue.y, slotValue.z, slotValue.w };
            bool changed = false;
            const char* label = param.label.empty() ? param.key.c_str() : param.label.c_str();
            if (param.ref.channel >= 4) {
                return false;
            }
            switch (param.type) {
            case VFX::ParamType::Float:
                changed = ImGui::DragFloat(label, &value[param.ref.channel], param.speed, param.minValues[0], param.maxValues[0]);
                break;
            case VFX::ParamType::Float2:
                if (param.ref.channel > 2) break;
                changed = ImGui::DragFloat2(label, &value[param.ref.channel], param.speed, param.minValues[0], param.maxValues[0]);
                break;
            case VFX::ParamType::Float3:
                if (param.ref.channel > 1) break;
                changed = ImGui::DragFloat3(label, &value[param.ref.channel], param.speed, param.minValues[0], param.maxValues[0]);
                break;
            case VFX::ParamType::Float4:
                if (param.ref.channel > 0) break;
                changed = ImGui::DragFloat4(label, &value[param.ref.channel], param.speed, param.minValues[0], param.maxValues[0]);
                break;
            case VFX::ParamType::Color:
                if (param.ref.channel > 0) break;
                changed = ImGui::ColorEdit4(label, &value[param.ref.channel]);
                break;
            case VFX::ParamType::Toggle: {
                bool enabled = value[param.ref.channel] >= 0.5f;
                if (ImGui::Checkbox(label, &enabled)) {
                    value[param.ref.channel] = enabled ? 1.0f : 0.0f;
                    changed = true;
                }
                break;
            }
            default:
                break;
            }
            if (changed) {
                slotValue = { value[0], value[1], value[2], value[3] };
            }
            return changed;
        }
#endif

    }

    void ModelComponent::SetModelHandle(ASSET::AssetHandle<ASSET::ModelAsset> handle) {
        modelHandle_ = handle;
        if (const ASSET::ModelAsset* model = ASSET::GetGlobalAssetRegistry().FindModel(handle)) {
            modelPath_ = model->sourcePath;
        }
    }

    ASSET::AssetHandle<ASSET::ModelAsset> ModelComponent::GetModelHandle() const {
        return modelHandle_;
    }

    void ModelComponent::SetVisible(bool visible) {
        visible_ = visible;
    }

    bool ModelComponent::IsVisible() const {
        return visible_;
    }

    void ModelComponent::SetCastShadow(bool castShadow) {
        castShadow_ = castShadow;
    }

    bool ModelComponent::CastShadow() const {
        return castShadow_;
    }

    void ModelComponent::SetReceiveShadow(bool receiveShadow) {
        receiveShadow_ = receiveShadow;
    }

    bool ModelComponent::ReceiveShadow() const {
        return receiveShadow_;
    }

    void ModelComponent::SetRenderLayerMask(uint32_t mask) {
        renderLayerMask_ = mask;
    }

    uint32_t ModelComponent::GetRenderLayerMask() const {
        return renderLayerMask_;
    }

    void ModelComponent::SetPostGroupMask(uint32_t mask) {
        postGroupMask_ = mask;
    }

    uint32_t ModelComponent::GetPostGroupMask() const {
        return postGroupMask_;
    }

    void ModelComponent::SetMaterialFxProfileId(std::string profileId) {
        materialFxProfileId_ = std::move(profileId);
        ResetMaterialFxToProfileDefaults();
    }

    const std::string& ModelComponent::GetMaterialFxProfileId() const {
        return materialFxProfileId_;
    }

    DirectX::XMFLOAT4(&ModelComponent::GetMaterialFxParamValues())[4] {
        return materialFxParamValues_;
    }

    const DirectX::XMFLOAT4(&ModelComponent::GetMaterialFxParamValues() const)[4] {
        return materialFxParamValues_;
    }

    bool ModelComponent::AreMaterialFxValuesInitialized() const {
        return materialFxValuesInitialized_;
    }

    bool ModelComponent::SetMaterialFxFloat(const std::string& key, float value) {
        if (materialFxProfileId_.empty()) {
            return false;
        }

        MaterialFxProfile profile{};
        if (!MaterialFxProfile::LoadById(materialFxProfileId_, profile)) {
            return false;
        }
        if (!EnsureMaterialFxValuesReady(materialFxProfileId_, materialFxParamValues_, materialFxValuesInitialized_)) {
            return false;
        }

        VFX::ParamChannelRef ref{};
        VFX::ParamType type = VFX::ParamType::Float;
        if (!ResolveParamRef(profile, key, ref, &type) || type != VFX::ParamType::Float) {
            return false;
        }
        if (ref.slot >= std::size(materialFxParamValues_) || ref.channel >= 4u) {
            return false;
        }
        float* dst = &materialFxParamValues_[ref.slot].x;
        dst[ref.channel] = value;
        materialFxValuesInitialized_ = true;
        return true;
    }

    bool ModelComponent::SetMaterialFxFloat2(const std::string& key, const DirectX::XMFLOAT2& value) {
        if (materialFxProfileId_.empty()) {
            return false;
        }
        MaterialFxProfile profile{};
        if (!MaterialFxProfile::LoadById(materialFxProfileId_, profile)) {
            return false;
        }
        if (!EnsureMaterialFxValuesReady(materialFxProfileId_, materialFxParamValues_, materialFxValuesInitialized_)) {
            return false;
        }

        VFX::ParamChannelRef ref{};
        VFX::ParamType type = VFX::ParamType::Float;
        if (!ResolveParamRef(profile, key, ref, &type) || type != VFX::ParamType::Float2 || ref.channel > 2u) {
            return false;
        }
        if (ref.slot >= std::size(materialFxParamValues_)) {
            return false;
        }
        float* dst = &materialFxParamValues_[ref.slot].x;
        dst[ref.channel] = value.x;
        dst[ref.channel + 1] = value.y;
        materialFxValuesInitialized_ = true;
        return true;
    }

    bool ModelComponent::SetMaterialFxFloat3(const std::string& key, const DirectX::XMFLOAT3& value) {
        if (materialFxProfileId_.empty()) {
            return false;
        }
        MaterialFxProfile profile{};
        if (!MaterialFxProfile::LoadById(materialFxProfileId_, profile)) {
            return false;
        }
        if (!EnsureMaterialFxValuesReady(materialFxProfileId_, materialFxParamValues_, materialFxValuesInitialized_)) {
            return false;
        }

        VFX::ParamChannelRef ref{};
        VFX::ParamType type = VFX::ParamType::Float;
        if (!ResolveParamRef(profile, key, ref, &type) || type != VFX::ParamType::Float3 || ref.channel > 1u) {
            return false;
        }
        if (ref.slot >= std::size(materialFxParamValues_)) {
            return false;
        }
        float* dst = &materialFxParamValues_[ref.slot].x;
        dst[ref.channel] = value.x;
        dst[ref.channel + 1] = value.y;
        dst[ref.channel + 2] = value.z;
        materialFxValuesInitialized_ = true;
        return true;
    }

    bool ModelComponent::SetMaterialFxFloat4(const std::string& key, const DirectX::XMFLOAT4& value) {
        if (materialFxProfileId_.empty()) {
            return false;
        }
        MaterialFxProfile profile{};
        if (!MaterialFxProfile::LoadById(materialFxProfileId_, profile)) {
            return false;
        }
        if (!EnsureMaterialFxValuesReady(materialFxProfileId_, materialFxParamValues_, materialFxValuesInitialized_)) {
            return false;
        }

        VFX::ParamChannelRef ref{};
        VFX::ParamType type = VFX::ParamType::Float;
        if (!ResolveParamRef(profile, key, ref, &type) || (type != VFX::ParamType::Float4 && type != VFX::ParamType::Color) || ref.channel > 0u) {
            return false;
        }
        if (ref.slot >= std::size(materialFxParamValues_)) {
            return false;
        }
        materialFxParamValues_[ref.slot] = value;
        materialFxValuesInitialized_ = true;
        return true;
    }

    bool ModelComponent::GetMaterialFxFloat(const std::string& key, float& out) const {
        if (materialFxProfileId_.empty()) {
            return false;
        }
        MaterialFxProfile profile{};
        if (!MaterialFxProfile::LoadById(materialFxProfileId_, profile)) {
            return false;
        }

        VFX::ParamChannelRef ref{};
        VFX::ParamType type = VFX::ParamType::Float;
        if (!ResolveParamRef(profile, key, ref, &type) || type != VFX::ParamType::Float) {
            return false;
        }
        if (ref.slot >= std::size(materialFxParamValues_) || ref.channel >= 4u) {
            return false;
        }

        const DirectX::XMFLOAT4* sourceValues = materialFxParamValues_;
        DirectX::XMFLOAT4 defaultValues[4]{};
        if (!materialFxValuesInitialized_) {
            profile.CopyValuesTo(defaultValues);
            sourceValues = defaultValues;
        }
        const float* src = &sourceValues[ref.slot].x;
        out = src[ref.channel];
        return true;
    }

    void ModelComponent::ResetMaterialFxToProfileDefaults() {
        materialFxValuesInitialized_ = false;
        for (DirectX::XMFLOAT4& value : materialFxParamValues_) {
            value = {};
        }
        if (materialFxProfileId_.empty()) {
            return;
        }
        MaterialFxProfile profile{};
        if (!MaterialFxProfile::LoadById(materialFxProfileId_, profile)) {
            return;
        }
        profile.CopyValuesTo(materialFxParamValues_);
        materialFxValuesInitialized_ = true;
    }

    void ModelComponent::Serialize(nlohmann::json& out) const {
        out["model"] = modelPath_;
        out["visible"] = visible_;
        out["castShadow"] = castShadow_;
        out["receiveShadow"] = receiveShadow_;
        out["renderLayerMask"] = renderLayerMask_;
        out["postGroupMask"] = postGroupMask_;
        out["materialFxProfileId"] = materialFxProfileId_;
        out["materialFxValuesInitialized"] = materialFxValuesInitialized_;
        out["materialFxParamValues"] = nlohmann::json::array();
        for (const DirectX::XMFLOAT4& value : materialFxParamValues_) {
            out["materialFxParamValues"].push_back(nlohmann::json::array({ value.x, value.y, value.z, value.w }));
        }
    }

    void ModelComponent::Deserialize(const nlohmann::json& in) {
        modelPath_ = in.value("model", modelPath_);
        if (!modelPath_.empty()) {
            modelHandle_ = ASSET::GetGlobalAssetRegistry().GetOrLoadModel(modelPath_);
        }
        visible_ = in.value("visible", visible_);
        castShadow_ = in.value("castShadow", castShadow_);
        receiveShadow_ = in.value("receiveShadow", receiveShadow_);
        renderLayerMask_ = in.value("renderLayerMask", renderLayerMask_);
        postGroupMask_ = in.value("postGroupMask", postGroupMask_);
        materialFxProfileId_ = in.value("materialFxProfileId", materialFxProfileId_);
        bool hasParamValues = false;
        if (in.contains("materialFxParamValues") && in["materialFxParamValues"].is_array()) {
            const auto& values = in["materialFxParamValues"];
            const size_t count = std::min<size_t>(values.size(), std::size(materialFxParamValues_));
            for (size_t i = 0; i < count; ++i) {
                const auto& node = values[i];
                if (!node.is_array() || node.size() < 4) {
                    continue;
                }
                materialFxParamValues_[i] = {
                    node[0].is_number() ? node[0].get<float>() : materialFxParamValues_[i].x,
                    node[1].is_number() ? node[1].get<float>() : materialFxParamValues_[i].y,
                    node[2].is_number() ? node[2].get<float>() : materialFxParamValues_[i].z,
                    node[3].is_number() ? node[3].get<float>() : materialFxParamValues_[i].w
                };
            }
            hasParamValues = true;
        }
        materialFxValuesInitialized_ = in.value("materialFxValuesInitialized", hasParamValues);
        if (!materialFxValuesInitialized_) {
            ResetMaterialFxToProfileDefaults();
        }
    }

    void ModelComponent::BuildInspector(IInspectorBuilder& builder) {
        builder.Bool("Visible", visible_);
        builder.Bool("Cast Shadow", castShadow_);
        builder.Bool("Receive Shadow", receiveShadow_);
        int layerMask = static_cast<int>(renderLayerMask_);
        if (builder.Int("Render Layer Mask", layerMask)) {
            renderLayerMask_ = static_cast<uint32_t>(std::max(layerMask, 0));
        }
        int postMask = static_cast<int>(postGroupMask_);
        if (builder.Int("Post Group Mask", postMask)) {
            postGroupMask_ = static_cast<uint32_t>(postMask < 0 ? 0 : postMask);
        }
        builder.String("Material FX Profile", materialFxProfileId_);
    }

    void ModelComponent::RenderImGui() {
#if defined(_DEBUG)
        ImGui::Checkbox("Visible", &visible_);
        ImGui::Checkbox("Cast Shadow", &castShadow_);
        ImGui::Checkbox("Receive Shadow", &receiveShadow_);
        int layerMask = static_cast<int>(renderLayerMask_);
        if (ImGui::InputInt("Render Layer Mask", &layerMask)) {
            renderLayerMask_ = static_cast<uint32_t>(std::max(layerMask, 0));
        }
        int postMask = static_cast<int>(postGroupMask_);
        if (ImGui::InputInt("Post Group Mask", &postMask)) {
            postGroupMask_ = static_cast<uint32_t>(postMask < 0 ? 0 : postMask);
        }
        char profileBuffer[256]{};
        const std::string previousProfileId = materialFxProfileId_;
        std::strncpy(profileBuffer, materialFxProfileId_.c_str(), sizeof(profileBuffer) - 1);
        if (ImGui::InputText("Material FX Profile", profileBuffer, sizeof(profileBuffer))) {
            SetMaterialFxProfileId(profileBuffer);
        }

        if (!materialFxProfileId_.empty()) {
            MaterialFxProfile profile{};
            if (MaterialFxProfile::LoadById(materialFxProfileId_, profile)) {
                const bool profileChanged = (materialFxProfileId_ != previousProfileId);
                if (profileChanged || !materialFxValuesInitialized_) {
                    profile.CopyValuesTo(materialFxParamValues_);
                    materialFxValuesInitialized_ = true;
                }

                if (ImGui::Button("Reset Material FX Defaults")) {
                    profile.CopyValuesTo(materialFxParamValues_);
                    materialFxValuesInitialized_ = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Reload Material FX Profile")) {
                    MaterialFxProfile reloadedProfile{};
                    if (MaterialFxProfile::LoadById(materialFxProfileId_, reloadedProfile)) {
                        profile = std::move(reloadedProfile);
                    }
                }

                if (ImGui::TreeNode("Material FX Parameters")) {
                    for (const VFX::ParamDesc& param : profile.params) {
                        const int slot = static_cast<int>(param.ref.slot);
                        if (slot < 0 || slot >= static_cast<int>(std::size(materialFxParamValues_)) || param.ref.channel >= 4) {
                            continue;
                        }
                        ImGui::PushID(param.key.c_str());
                        DrawParamControl(param, materialFxParamValues_[slot]);
                        ImGui::PopID();
                    }
                    ImGui::TreePop();
                }
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Material FX profile not found: %s", materialFxProfileId_.c_str());
            }
        }

        if (ImGui::TreeNode("Advanced Raw Material FX Block (4x float4)")) {
            for (size_t i = 0; i < std::size(materialFxParamValues_); ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::InputFloat4("Param", &materialFxParamValues_[i].x);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        auto* model = ASSET::GetGlobalAssetRegistry().FindModel(modelHandle_);
        if (model == nullptr) {
            ImGui::TextUnformatted("Model: <none>");
            return;
        }

        ImGui::Text("Model: %s", model->name.c_str());
        ImGui::Text("Source: %s", model->sourcePath.c_str());
        ImGui::Text("State: %d", static_cast<int>(model->state));
        ImGui::Text("Primitive Count: %u", static_cast<unsigned>(model->primitives.size()));
#endif
    }

} // namespace HIKARI
