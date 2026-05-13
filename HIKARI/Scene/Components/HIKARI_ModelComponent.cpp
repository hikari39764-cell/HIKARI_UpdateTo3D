#include "HIKARI_ModelComponent.h"

#include "Assets/HIKARI_AssetTypes.h"
#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/Core/HIKARI_ModelAsset.h"
#include "Render3D/Debug/HIKARI_MeshWireDebugRenderer.h"
#include "Render3D/Procedural/HIKARI_ProceduralModelFactory.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
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

        const char* ToString(ModelSourceKind kind) {
            return kind == ModelSourceKind::Procedural ? "Procedural" : "Asset";
        }

        const char* ToString(ProceduralMeshKind kind) {
            switch (kind) {
            case ProceduralMeshKind::Plane: return "Plane";
            case ProceduralMeshKind::GridPlane: return "GridPlane";
            case ProceduralMeshKind::Box: return "Box";
            case ProceduralMeshKind::Sphere: return "Sphere";
            default: return "GridPlane";
            }
        }

        const char* ToString(ModelRenderDebugMode mode) {
            switch (mode) {
            case ModelRenderDebugMode::WireOverlay: return "WireOverlay";
            case ModelRenderDebugMode::WireOnly: return "WireOnly";
            case ModelRenderDebugMode::BoundsOnly: return "BoundsOnly";
            case ModelRenderDebugMode::Normal:
            default: return "Normal";
            }
        }

        ModelSourceKind ParseModelSourceKind(const nlohmann::json& in, ModelSourceKind fallback) {
            const std::string value = in.is_string() ? in.get<std::string>() : std::string{};
            if (value == "Procedural") return ModelSourceKind::Procedural;
            if (value == "Asset") return ModelSourceKind::Asset;
            return fallback;
        }

        ProceduralMeshKind ParseProceduralMeshKind(const nlohmann::json& in, ProceduralMeshKind fallback) {
            const std::string value = in.is_string() ? in.get<std::string>() : std::string{};
            if (value == "Plane") return ProceduralMeshKind::Plane;
            if (value == "GridPlane") return ProceduralMeshKind::GridPlane;
            if (value == "Box") return ProceduralMeshKind::Box;
            if (value == "Sphere") return ProceduralMeshKind::Sphere;
            return fallback;
        }

        ModelRenderDebugMode ParseRenderDebugMode(const nlohmann::json& in, ModelRenderDebugMode fallback) {
            const std::string value = in.is_string() ? in.get<std::string>() : std::string{};
            if (value == "WireOverlay") return ModelRenderDebugMode::WireOverlay;
            if (value == "WireOnly") return ModelRenderDebugMode::WireOnly;
            if (value == "BoundsOnly") return ModelRenderDebugMode::BoundsOnly;
            if (value == "Normal") return ModelRenderDebugMode::Normal;
            return fallback;
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
        const char* ToAlphaModeText(AlphaMode mode) {
            switch (mode) {
            case AlphaMode::Opaque: return "Opaque";
            case AlphaMode::Mask: return "Mask";
            case AlphaMode::Blend: return "Blend";
            default: return "Unknown";
            }
        }

        const char* ResolveTexturePathDebug(const ModelAsset& asset, const TextureSlot& slot) {
            if (slot.textureIndex < 0 || slot.textureIndex >= static_cast<int>(asset.textures.size())) {
                return "<none>";
            }
            const std::string& path = asset.textures[static_cast<size_t>(slot.textureIndex)].sourcePath;
            return path.empty() ? "<empty>" : path.c_str();
        }

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
            case VFX::ParamType::Color3:
                if (param.ref.channel > 1) break;
                changed = ImGui::ColorEdit3(label, &value[param.ref.channel]);
                break;
            case VFX::ParamType::Color:
            case VFX::ParamType::Color4:
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

        const char* ToStateText(ModelAsset::State state) {
            switch (state) {
            case ModelAsset::State::Unloaded:
                return "Unloaded";
            case ModelAsset::State::Loaded:
                return "Loaded";
            case ModelAsset::State::Failed:
                return "Failed";
            default:
                return "Unknown";
            }
        }
    }

    void ModelComponent::SetModelAsset(ModelAsset* asset) {
        asset_ = asset;
        if (asset_) {
            assetId_ = asset_->GetName();
        }
    }

    ModelAsset* ModelComponent::GetModelAsset() {
        return asset_;
    }

    const ModelAsset* ModelComponent::GetModelAsset() const {
        return asset_;
    }

    void ModelComponent::SetAsset(ModelAsset* asset) {
        SetModelAsset(asset);
    }

    ModelAsset* ModelComponent::GetAsset() {
        return GetModelAsset();
    }

    const ModelAsset* ModelComponent::GetAsset() const {
        return GetModelAsset();
    }

    void ModelComponent::SetVisible(bool visible) {
        visible_ = visible;
    }

    bool ModelComponent::IsVisible() const {
        return visible_;
    }

    void ModelComponent::SetSkeletonDebugVisible(bool visible) {
        showSkeletonDebug_ = visible;
    }

    bool ModelComponent::IsSkeletonDebugVisible() const {
        return showSkeletonDebug_;
    }

    void ModelComponent::SetSkeletonDebugXRay(bool enabled) {
        skeletonDebugXRay_ = enabled;
    }

    bool ModelComponent::IsSkeletonDebugXRay() const {
        return skeletonDebugXRay_;
    }

    void ModelComponent::SetCastShadow(bool enabled) {
        castShadow_ = enabled;
    }

    bool ModelComponent::GetCastShadow() const {
        return castShadow_;
    }

    void ModelComponent::SetReceiveShadow(bool enabled) {
        receiveShadow_ = enabled;
    }

    bool ModelComponent::GetReceiveShadow() const {
        return receiveShadow_;
    }

    void ModelComponent::SetSourceKind(ModelSourceKind kind) { sourceKind_ = kind; }
    ModelSourceKind ModelComponent::GetSourceKind() const { return sourceKind_; }
    void ModelComponent::SetProceduralSettings(const ProceduralModelSettings& settings) { procedural_ = settings; }
    const ProceduralModelSettings& ModelComponent::GetProceduralSettings() const { return procedural_; }
    void ModelComponent::SetRenderDebugMode(ModelRenderDebugMode mode) { debugRenderMode_ = mode; }
    ModelRenderDebugMode ModelComponent::GetRenderDebugMode() const { return debugRenderMode_; }
    void ModelComponent::SetWireColor(uint32_t color) { wireColor_ = color; }
    uint32_t ModelComponent::GetWireColor() const { return wireColor_; }
    void ModelComponent::SetMaxWireLines(uint32_t count) { maxWireLines_ = count; }
    uint32_t ModelComponent::GetMaxWireLines() const { return maxWireLines_; }
    bool ModelComponent::GetWirePerPrimitiveColor() const { return wirePerPrimitiveColor_; }

    const std::string& ModelComponent::GetAssetId() const {
        return assetId_;
    }

    void ModelComponent::SetAssetId(std::string assetId) {
        assetId_ = std::move(assetId);
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
        if (!ResolveParamRef(profile, key, ref, &type) || (type != VFX::ParamType::Float4 && type != VFX::ParamType::Color && type != VFX::ParamType::Color4) || ref.channel > 0u) {
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
        out["assetId"] = assetId_;
        out["visible"] = visible_;
        out["showSkeletonDebug"] = showSkeletonDebug_;
        out["skeletonDebugXRay"] = skeletonDebugXRay_;
        out["castShadow"] = castShadow_;
        out["receiveShadow"] = receiveShadow_;
        out["sourceKind"] = ToString(sourceKind_);
        out["procedural"] = {
            { "kind", ToString(procedural_.kind) },
            { "width", procedural_.width },
            { "height", procedural_.height },
            { "depth", procedural_.depth },
            { "segmentsX", procedural_.segmentsX },
            { "segmentsY", procedural_.segmentsY },
            { "segmentsZ", procedural_.segmentsZ },
            { "sphereSlices", procedural_.sphereSlices },
            { "sphereStacks", procedural_.sphereStacks },
            { "doubleSided", procedural_.doubleSided },
            { "generateTangents", procedural_.generateTangents }
        };
        out["debugRenderMode"] = ToString(debugRenderMode_);
        out["wireColor"] = wireColor_;
        out["maxWireLines"] = maxWireLines_;
        out["wirePerPrimitiveColor"] = wirePerPrimitiveColor_;
        out["postGroupMask"] = postGroupMask_;
        out["materialFxProfileId"] = materialFxProfileId_;
        out["materialFxValuesInitialized"] = materialFxValuesInitialized_;
        out["materialFxParamValues"] = nlohmann::json::array();
        for (const DirectX::XMFLOAT4& value : materialFxParamValues_) {
            out["materialFxParamValues"].push_back(nlohmann::json::array({ value.x, value.y, value.z, value.w }));
        }
    }

    void ModelComponent::Deserialize(const nlohmann::json& in) {
        assetId_ = in.value("assetId", assetId_);
        visible_ = in.value("visible", visible_);
        showSkeletonDebug_ = in.value("showSkeletonDebug", showSkeletonDebug_);
        skeletonDebugXRay_ = in.value("skeletonDebugXRay", skeletonDebugXRay_);
        castShadow_ = in.value("castShadow", castShadow_);
        receiveShadow_ = in.value("receiveShadow", receiveShadow_);
        sourceKind_ = ParseModelSourceKind(in.value("sourceKind", nlohmann::json{}), sourceKind_);
        if (in.contains("procedural") && in["procedural"].is_object()) {
            const auto& node = in["procedural"];
            procedural_.kind = ParseProceduralMeshKind(node.value("kind", nlohmann::json{}), procedural_.kind);
            procedural_.width = node.value("width", procedural_.width);
            procedural_.height = node.value("height", procedural_.height);
            procedural_.depth = node.value("depth", procedural_.depth);
            procedural_.segmentsX = node.value("segmentsX", procedural_.segmentsX);
            procedural_.segmentsY = node.value("segmentsY", procedural_.segmentsY);
            procedural_.segmentsZ = node.value("segmentsZ", procedural_.segmentsZ);
            procedural_.sphereSlices = node.value("sphereSlices", procedural_.sphereSlices);
            procedural_.sphereStacks = node.value("sphereStacks", procedural_.sphereStacks);
            procedural_.doubleSided = node.value("doubleSided", procedural_.doubleSided);
            procedural_.generateTangents = node.value("generateTangents", procedural_.generateTangents);
        }
        debugRenderMode_ = ParseRenderDebugMode(in.value("debugRenderMode", nlohmann::json{}), debugRenderMode_);
        wireColor_ = in.value("wireColor", wireColor_);
        maxWireLines_ = in.value("maxWireLines", maxWireLines_);
        wirePerPrimitiveColor_ = in.value("wirePerPrimitiveColor", wirePerPrimitiveColor_);
        postGroupMask_ = in.value("postGroupMask", postGroupMask_);
        materialFxProfileId_ = in.value("materialFxProfileId", materialFxProfileId_);
        bool hasParamValues = false;
        if (in.contains("materialFxParamValues") && in["materialFxParamValues"].is_array()) {
            const auto& values = in["materialFxParamValues"];
            const size_t count = (std::min)(values.size(), std::size(materialFxParamValues_));
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
        int sourceKind = static_cast<int>(sourceKind_);
        if (builder.Int("Source Kind (0=Asset, 1=Procedural)", sourceKind)) {
            sourceKind_ = static_cast<ModelSourceKind>((std::clamp)(sourceKind, 0, 1));
        }
        if (sourceKind_ == ModelSourceKind::Procedural) {
            int proceduralKind = static_cast<int>(procedural_.kind);
            if (builder.Int("Procedural Kind (0=Plane, 1=Grid, 2=Box, 3=Sphere)", proceduralKind)) {
                procedural_.kind = static_cast<ProceduralMeshKind>((std::clamp)(proceduralKind, 0, 3));
            }
            builder.Float("Procedural Width", procedural_.width);
            builder.Float("Procedural Height", procedural_.height);
            builder.Float("Procedural Depth", procedural_.depth);
            int segmentsX = static_cast<int>(procedural_.segmentsX);
            int segmentsY = static_cast<int>(procedural_.segmentsY);
            if (builder.Int("Segments X", segmentsX)) {
                procedural_.segmentsX = static_cast<uint32_t>((std::clamp)(segmentsX, 1, 256));
            }
            if (builder.Int("Segments Y", segmentsY)) {
                procedural_.segmentsY = static_cast<uint32_t>((std::clamp)(segmentsY, 1, 256));
            }
            builder.Bool("Procedural Double Sided", procedural_.doubleSided);
            builder.Bool("Generate Tangents", procedural_.generateTangents);
        } else {
            builder.AssetIdPicker("Model Asset", AssetType::Model, assetId_);
        }
        int postMask = static_cast<int>(postGroupMask_);
        if (builder.Int("Post Group Mask", postMask)) {
            postGroupMask_ = static_cast<uint32_t>(postMask < 0 ? 0 : postMask);
        }
        builder.String("Material FX Profile", materialFxProfileId_);
    }

    void ModelComponent::RenderImGui() {
#if defined(_DEBUG)
        if (ImGui::TreeNodeEx("Model Source", ImGuiTreeNodeFlags_DefaultOpen)) {
            int sourceKind = static_cast<int>(sourceKind_);
            const char* sourceNames[] = { "Asset", "Procedural" };
            if (ImGui::Combo("Source Kind", &sourceKind, sourceNames, 2)) {
                sourceKind_ = static_cast<ModelSourceKind>((std::clamp)(sourceKind, 0, 1));
            }
            if (sourceKind_ == ModelSourceKind::Procedural) {
                int kind = static_cast<int>(procedural_.kind);
                const char* kindNames[] = { "Plane", "GridPlane", "Box", "Sphere" };
                if (ImGui::Combo("Procedural Kind", &kind, kindNames, 4)) {
                    procedural_.kind = static_cast<ProceduralMeshKind>((std::clamp)(kind, 0, 3));
                }
                ImGui::DragFloat("Width", &procedural_.width, 0.1f, 0.01f, 10000.0f);
                ImGui::DragFloat("Height", &procedural_.height, 0.1f, 0.01f, 10000.0f);
                ImGui::DragFloat("Depth", &procedural_.depth, 0.1f, 0.01f, 10000.0f);
                int sx = static_cast<int>(procedural_.segmentsX);
                int sy = static_cast<int>(procedural_.segmentsY);
                int sz = static_cast<int>(procedural_.segmentsZ);
                if (ImGui::DragInt("Segments X", &sx, 1.0f, 1, 256)) procedural_.segmentsX = static_cast<uint32_t>((std::clamp)(sx, 1, 256));
                if (ImGui::DragInt("Segments Y", &sy, 1.0f, 1, 256)) procedural_.segmentsY = static_cast<uint32_t>((std::clamp)(sy, 1, 256));
                if (ImGui::DragInt("Segments Z", &sz, 1.0f, 1, 64)) procedural_.segmentsZ = static_cast<uint32_t>((std::clamp)(sz, 1, 64));
                ImGui::Checkbox("Double Sided", &procedural_.doubleSided);
                ImGui::Checkbox("Generate Tangents", &procedural_.generateTangents);
                if (procedural_.kind == ProceduralMeshKind::Sphere) {
                    ImGui::TextDisabled("Sphere currently falls back to Box generation.");
                }
            }
            int debugMode = static_cast<int>(debugRenderMode_);
            const char* debugModes[] = { "Normal", "WireOverlay", "WireOnly", "BoundsOnly" };
            if (ImGui::Combo("Debug Render Mode", &debugMode, debugModes, 4)) {
                debugRenderMode_ = static_cast<ModelRenderDebugMode>((std::clamp)(debugMode, 0, 3));
            }
            ImGui::InputScalar("Wire Color RGBA", ImGuiDataType_U32, &wireColor_);
            int maxWireLines = static_cast<int>(maxWireLines_);
            if (ImGui::DragInt("Max Wire Lines", &maxWireLines, 100.0f, 0, 1000000)) {
                maxWireLines_ = static_cast<uint32_t>((std::max)(0, maxWireLines));
            }
            ImGui::Checkbox("Wire Per Primitive Color", &wirePerPrimitiveColor_);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Material FX / Post", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Visible", &visible_);
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
                        MaterialFxProfile::ClearCache();
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
            ImGui::TreePop();
        }

        if (asset_ == nullptr && sourceKind_ == ModelSourceKind::Asset) {
            ImGui::TextUnformatted("Asset: <none>");
            return;
        }

        if (ImGui::TreeNodeEx("Shadow", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Cast Shadow", &castShadow_);
            ImGui::Checkbox("Receive Shadow", &receiveShadow_);
            ImGui::TreePop();
        }

        if (asset_ == nullptr) {
            ImGui::TextUnformatted("Procedural asset is generated at render submission time.");
            return;
        }

        const size_t matrixNodeCount = static_cast<size_t>(std::count_if(asset_->nodes.begin(), asset_->nodes.end(), [](const ModelNode& node) {
            return node.hasLocalMatrix;
        }));
        const size_t skinNodeCount = static_cast<size_t>(std::count_if(asset_->nodes.begin(), asset_->nodes.end(), [](const ModelNode& node) {
            return node.skinIndex >= 0;
        }));
        size_t meshCount = asset_->meshes.size();
        size_t primitiveCount = 0;
        size_t skinnedPrimitiveCount = 0;
        size_t skinnedVertexCount = 0;
        const SkinnedVertex3D* firstSkinnedVertex = nullptr;
        for (const MeshAsset& mesh : asset_->meshes) {
            primitiveCount += mesh.primitives.size();
            for (const MeshPrimitive& primitive : mesh.primitives) {
                if (!primitive.skinnedVertices.empty()) {
                    ++skinnedPrimitiveCount;
                    skinnedVertexCount += primitive.skinnedVertices.size();
                    if (firstSkinnedVertex == nullptr) {
                        firstSkinnedVertex = &primitive.skinnedVertices.front();
                    }
                }
            }
        }

        if (ImGui::TreeNodeEx("Model Basic", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Asset: %s", asset_->GetName().c_str());
            ImGui::Text("Source: %s", asset_->GetSourcePath().c_str());
            ImGui::Text("State: %s", ToStateText(asset_->GetState()));
            ImGui::Text("Has Mesh: %s", asset_->GetMesh() ? "Yes" : "No");
            ImGui::Text("Has Material: %s", asset_->GetMaterial() ? "Yes" : "No");
            if (const Material* material = asset_->GetMaterial()) {
                const MATH::Vec4& color = material->GetBaseColor();
                ImGui::Text("BaseColor: (%.2f, %.2f, %.2f, %.2f)", color.x, color.y, color.z, color.w);
                const char* texturePath = material->GetBaseColorTexturePath().empty() ? "<none>" : material->GetBaseColorTexturePath().c_str();
                ImGui::Text("TexturePath: %s", texturePath);
                ImGui::Text("TextureHandle: %d (%s)",
                    material->GetBaseColorTextureHandle(),
                    material->HasBaseColorTexture() ? "Valid" : "Invalid");
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Model Structure", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Nodes: %zu", asset_->nodes.size());
            ImGui::Text("Matrix Nodes: %zu", matrixNodeCount);
            ImGui::Text("Mesh Count: %zu", meshCount);
            ImGui::Text("Primitive Count: %zu", primitiveCount);
            ImGui::Text("Materials: %zu", asset_->materials.size());
            ImGui::Text("Textures: %zu", asset_->textures.size());
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Materials")) {
            for (size_t materialIndex = 0; materialIndex < asset_->materials.size(); ++materialIndex) {
                const MaterialAsset& material = asset_->materials[materialIndex];
                ImGui::PushID(static_cast<int>(materialIndex));
                const char* materialName = material.name.empty() ? "<unnamed>" : material.name.c_str();
                if (ImGui::TreeNode("Material", "Material[%zu] %s", materialIndex, materialName)) {
                    ImGui::Text("Base Color Factor: %.3f %.3f %.3f %.3f",
                        material.baseColorFactor.x,
                        material.baseColorFactor.y,
                        material.baseColorFactor.z,
                        material.baseColorFactor.w);
                    ImGui::Text("Base Color Texture: index=%d texCoord=%d path=%s",
                        material.baseColorTexture.textureIndex,
                        material.baseColorTexture.texCoord,
                        ResolveTexturePathDebug(*asset_, material.baseColorTexture));
                    ImGui::Text("Metallic / Roughness: %.3f / %.3f", material.metallicFactor, material.roughnessFactor);
                    ImGui::Text("Metallic Roughness Texture: index=%d texCoord=%d path=%s",
                        material.metallicRoughnessTexture.textureIndex,
                        material.metallicRoughnessTexture.texCoord,
                        ResolveTexturePathDebug(*asset_, material.metallicRoughnessTexture));
                    ImGui::Text("Normal Texture: index=%d texCoord=%d scale=%.3f path=%s",
                        material.normalTexture.textureIndex,
                        material.normalTexture.texCoord,
                        material.normalTexture.scale,
                        ResolveTexturePathDebug(*asset_, material.normalTexture));
                    ImGui::Text("Occlusion Texture: index=%d texCoord=%d strength=%.3f path=%s",
                        material.occlusionTexture.textureIndex,
                        material.occlusionTexture.texCoord,
                        material.occlusionTexture.strength,
                        ResolveTexturePathDebug(*asset_, material.occlusionTexture));
                    ImGui::Text("Emissive Factor: %.3f %.3f %.3f",
                        material.emissiveFactor.x,
                        material.emissiveFactor.y,
                        material.emissiveFactor.z);
                    ImGui::Text("Emissive Strength: %.3f", material.emissiveStrength);
                    ImGui::Text("Emissive Texture: index=%d texCoord=%d path=%s",
                        material.emissiveTexture.textureIndex,
                        material.emissiveTexture.texCoord,
                        ResolveTexturePathDebug(*asset_, material.emissiveTexture));
                    ImGui::Text("Alpha Mode: %s", ToAlphaModeText(material.alphaMode));
                    ImGui::Text("Alpha Cutoff: %.3f", material.alphaCutoff);
                    ImGui::Text("Double Sided: %s", material.doubleSided ? "Yes" : "No");
                    ImGui::Text("Feature Bits: 0x%08X", material.featureBits);
                    ImGui::Text("Unlit: %s", (material.featureBits & MATERIAL_FEATURES::Unlit) != 0 ? "Yes" : "No");
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Skinning", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Skin Nodes: %zu", skinNodeCount);
            ImGui::Checkbox("Show Skeleton Debug", &showSkeletonDebug_);
            ImGui::Checkbox("Skeleton Debug XRay", &skeletonDebugXRay_);
            ImGui::Text("Skins: %zu", asset_->GetSkinCount());
            ImGui::Text("Skinned Mesh: %s", asset_->HasSkinnedMesh() ? "Yes" : "No");
            ImGui::Text("Skinned Primitive Count: %zu", skinnedPrimitiveCount);
            ImGui::Text("Skinned Vertex Count: %zu", skinnedVertexCount);
            if (firstSkinnedVertex != nullptr) {
                ImGui::Text("First Joints: %u %u %u %u",
                    static_cast<unsigned>(firstSkinnedVertex->joints[0]),
                    static_cast<unsigned>(firstSkinnedVertex->joints[1]),
                    static_cast<unsigned>(firstSkinnedVertex->joints[2]),
                    static_cast<unsigned>(firstSkinnedVertex->joints[3]));
                ImGui::Text("First Weights: %.3f %.3f %.3f %.3f",
                    firstSkinnedVertex->weights[0],
                    firstSkinnedVertex->weights[1],
                    firstSkinnedVertex->weights[2],
                    firstSkinnedVertex->weights[3]);
            }
            for (size_t skinIndex = 0; skinIndex < asset_->skins.size(); ++skinIndex) {
                const SkeletonAsset& skin = asset_->skins[skinIndex];
                ImGui::PushID(static_cast<int>(skinIndex));
                if (ImGui::TreeNode("Skin", "Skin[%zu] %s", skinIndex, skin.name.c_str())) {
                    ImGui::Text("Skeleton Root Node: %d", skin.skeletonRootNode);
                    ImGui::Text("Joint Count: %zu", skin.joints.size());
                    const size_t maxDebugJoints = (std::min)(skin.joints.size(), static_cast<size_t>(32));
                    for (size_t jointIndex = 0; jointIndex < maxDebugJoints; ++jointIndex) {
                        const SkeletonJoint& joint = skin.joints[jointIndex];
                        ImGui::BulletText("[%zu] %s node=%d parentJoint=%d", jointIndex, joint.name.c_str(), joint.nodeIndex, joint.parentJoint);
                    }
                    if (skin.joints.size() > maxDebugJoints) {
                        ImGui::Text("... %zu more joints", skin.joints.size() - maxDebugJoints);
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        const MODELRENDERER::ModelRendererDebugStats& rendererStats = MODELRENDERER::GetDebugStats();
        const MESHRENDERER::MeshRendererDebugStats& meshRendererStats = MESHRENDERER::GetDebugStats();

        if (ImGui::TreeNodeEx("Runtime Skinning", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Joint Palette Built: %s", rendererStats.builtPaletteCount > 0 ? "Yes" : "No");
            ImGui::Text("Skinned Nodes Rendered: %zu", rendererStats.skinnedNodeCount);
            ImGui::Text("Built Palettes: %zu", rendererStats.builtPaletteCount);
            ImGui::Text("Total Joint Matrices: %zu", rendererStats.totalJointMatrixCount);
            ImGui::Text("Last Skin Index: %d", rendererStats.lastSkinIndex);
            ImGui::Text("Last Palette Joint Count: %zu", rendererStats.lastPaletteJointCount);
            ImGui::Text("Skinned GPU Draws: %zu", meshRendererStats.skinnedGpuDrawCount);
            ImGui::Text("Skinned Fallbacks: %zu", meshRendererStats.skinnedFallbackCount);
            ImGui::Text("Uploaded Joints: %zu", meshRendererStats.uploadedJointCount);
            ImGui::Text("Max Joint Count: %zu", meshRendererStats.maxJointCount);
            ImGui::Text("Last Skinned Vertex Count: %zu", meshRendererStats.lastSkinnedVertexCount);
            if (rendererStats.hasFirstJointMatrix) {
                const MATH::Mat4& m = rendererStats.firstJointMatrix;
                ImGui::Text("First Joint Matrix:");
                ImGui::Text("[%.3f %.3f %.3f %.3f]", m.m[0][0], m.m[1][0], m.m[2][0], m.m[3][0]);
                ImGui::Text("[%.3f %.3f %.3f %.3f]", m.m[0][1], m.m[1][1], m.m[2][1], m.m[3][1]);
                ImGui::Text("[%.3f %.3f %.3f %.3f]", m.m[0][2], m.m[1][2], m.m[2][2], m.m[3][2]);
                ImGui::Text("[%.3f %.3f %.3f %.3f]", m.m[0][3], m.m[1][3], m.m[2][3], m.m[3][3]);
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Performance Stats")) {
            ImGui::TextUnformatted("ModelRenderer:");
            ImGui::Text("Submitted Model Items: %zu", rendererStats.submittedModelItemCount);
            ImGui::Text("Structured Models: %zu", rendererStats.structuredModelCount);
            ImGui::Text("Animated Local Builds: %zu", rendererStats.animatedLocalBuildCount);
            ImGui::Text("Sampled Channels: %zu", rendererStats.sampledChannelCount);
            ImGui::Text("Sampled Key Searches: %zu", rendererStats.sampledKeySearchCount);
            ImGui::Text("Node Global Matrix Builds: %zu", rendererStats.nodeGlobalMatrixBuildCount);
            ImGui::Text("Node Global Matrix Count: %zu", rendererStats.nodeGlobalMatrixCount);
            ImGui::Text("Joint Palette Builds: %zu", rendererStats.jointPaletteBuildCount);
            ImGui::Text("Joint Palette Matrix Count: %zu", rendererStats.jointPaletteMatrixCount);
            ImGui::Text("Expanded Mesh Cache Hit / Miss: %zu / %zu",
                rendererStats.expandedMeshCacheHitCount,
                rendererStats.expandedMeshCacheMissCount);
            ImGui::Text("Skeleton Debug Lines: %zu", rendererStats.skeletonDebugLineCount);
            ImGui::Text("Pose Cache Hit / Miss: %zu / %zu",
                rendererStats.poseCacheHitCount,
                rendererStats.poseCacheMissCount);
            ImGui::Text("Pose Updated / Reused: %zu / %zu",
                rendererStats.poseUpdatedCount,
                rendererStats.poseReusedCount);
            ImGui::Text("Animation LOD Near / Mid / Far / Very Far: %zu / %zu / %zu / %zu",
                rendererStats.lodNearCount,
                rendererStats.lodMidCount,
                rendererStats.lodFarCount,
                rendererStats.lodVeryFarCount);
            ImGui::Text("Joint Palette Cache Hit / Miss: %zu / %zu",
                rendererStats.jointPaletteCacheHitCount,
                rendererStats.jointPaletteCacheMissCount);

            ImGui::Separator();
            ImGui::TextUnformatted("MeshRenderer:");
            ImGui::Text("Static Draw Items: %zu", meshRendererStats.staticDrawItemCount);
            ImGui::Text("Skinned Draw Items: %zu", meshRendererStats.skinnedDrawItemCount);
            ImGui::Text("Wire Draw Items / GPU Draws: %zu / %zu",
                meshRendererStats.wireDrawItemCount,
                meshRendererStats.wireGpuDrawCount);
            ImGui::Text("Primitive Mesh Cache Hit / Miss: %zu / %zu",
                meshRendererStats.primitiveMeshCacheHitCount,
                meshRendererStats.primitiveMeshCacheMissCount);
            ImGui::Text("Skinned Primitive Mesh Cache Hit / Miss: %zu / %zu",
                meshRendererStats.primitiveSkinnedMeshCacheHitCount,
                meshRendererStats.primitiveSkinnedMeshCacheMissCount);
            ImGui::Text("Texture Cache Hit / Miss: %zu / %zu",
                meshRendererStats.materialTextureCacheHitCount,
                meshRendererStats.materialTextureCacheMissCount);
            ImGui::Text("PSO Cache Hit / Miss: %zu / %zu",
                meshRendererStats.psoCacheHitCount,
                meshRendererStats.psoCacheMissCount);
            ImGui::Text("MaterialFx Profile Cache Hit / Miss / Fail: %zu / %zu / %zu",
                meshRendererStats.materialFxProfileCacheHitCount,
                meshRendererStats.materialFxProfileCacheMissCount,
                meshRendererStats.materialFxProfileCacheFailCount);
            ImGui::Text("NormalMapped Primitives: %zu", meshRendererStats.normalMappedPrimitiveCount);
            ImGui::Text("NormalMap Fallbacks: %zu", meshRendererStats.normalMapFallbackCount);
            ImGui::Text("NormalTexture Cache Hit / Miss: %zu / %zu",
                meshRendererStats.normalTextureCacheHitCount,
                meshRendererStats.normalTextureCacheMissCount);
            ImGui::Text("Emissive Mapped Primitives: %zu", meshRendererStats.emissiveMappedPrimitiveCount);
            ImGui::Text("EmissiveMap Fallbacks: %zu", meshRendererStats.emissiveMapFallbackCount);
            ImGui::Text("EmissiveTexture Cache Hit / Miss: %zu / %zu",
                meshRendererStats.emissiveTextureCacheHitCount,
                meshRendererStats.emissiveTextureCacheMissCount);
            const PROCEDURAL::ProceduralModelDebugStats& proceduralStats = PROCEDURAL::GetDebugStats();
            ImGui::Separator();
            ImGui::TextUnformatted("Procedural:");
            ImGui::Text("Cache Hit / Miss: %zu / %zu", proceduralStats.cacheHitCount, proceduralStats.cacheMissCount);
            ImGui::Text("Generated Models: %zu", proceduralStats.generatedModelCount);
            ImGui::Text("Generated Vertices / Indices: %zu / %zu",
                proceduralStats.generatedVertexCount,
                proceduralStats.generatedIndexCount);
            const MESHWIREDEBUG::MeshWireDebugStats& wireStats = MESHWIREDEBUG::GetDebugStats();
            ImGui::Separator();
            ImGui::TextUnformatted("Wire Debug:");
            ImGui::Text("Submitted Models / Lines: %zu / %zu", wireStats.submittedModelCount, wireStats.submittedLineCount);
            ImGui::Text("Truncated Models: %zu", wireStats.truncatedModelCount);
            ImGui::Text("Cache Hit / Miss: %zu / %zu", wireStats.cacheHitCount, wireStats.cacheMissCount);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Animation Clips", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Text("Animation clips: %zu", asset_->animations.size());
            for (const AnimationClip& clip : asset_->animations) {
                ImGui::BulletText("%s (%.2fs)", clip.name.c_str(), clip.durationSec);
            }
            ImGui::TreePop();
        }
#endif
    }

} // namespace HIKARI
