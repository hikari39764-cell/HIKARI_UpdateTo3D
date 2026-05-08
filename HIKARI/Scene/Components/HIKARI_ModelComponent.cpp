#include "HIKARI_ModelComponent.h"

#include "Assets/HIKARI_AssetTypes.h"
#include "Editor/Inspectors/HIKARI_IInspectorBuilder.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_ModelAsset.h"
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
        out["assetId"] = assetId_;
        out["visible"] = visible_;
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
        int postMask = static_cast<int>(postGroupMask_);
        if (builder.Int("Post Group Mask", postMask)) {
            postGroupMask_ = static_cast<uint32_t>(postMask < 0 ? 0 : postMask);
        }
        builder.String("Material FX Profile", materialFxProfileId_);
        builder.AssetIdPicker("Model Asset", AssetType::Model, assetId_);
    }

    void ModelComponent::RenderImGui() {
#if defined(_DEBUG)
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
        if (asset_ == nullptr) {
            ImGui::TextUnformatted("Asset: <none>");
            return;
        }

        ImGui::Text("Asset: %s", asset_->GetName().c_str());
        ImGui::Text("Source: %s", asset_->GetSourcePath().c_str());
        ImGui::Text("State: %s", ToStateText(asset_->GetState()));
        ImGui::Text("Has Mesh: %s", asset_->GetMesh() ? "Yes" : "No");
        ImGui::Text("Has Material: %s", asset_->GetMaterial() ? "Yes" : "No");
        const size_t matrixNodeCount = static_cast<size_t>(std::count_if(asset_->nodes.begin(), asset_->nodes.end(), [](const ModelNode& node) {
            return node.hasLocalMatrix;
        }));
        const size_t skinNodeCount = static_cast<size_t>(std::count_if(asset_->nodes.begin(), asset_->nodes.end(), [](const ModelNode& node) {
            return node.skinIndex >= 0;
        }));
        ImGui::Text("Nodes: %zu", asset_->nodes.size());
        ImGui::Text("Matrix Nodes: %zu", matrixNodeCount);
        ImGui::Text("Skin Nodes: %zu", skinNodeCount);
        ImGui::Text("Skins: %zu", asset_->GetSkinCount());
        for (size_t skinIndex = 0; skinIndex < asset_->skins.size(); ++skinIndex) {
            const SkeletonAsset& skin = asset_->skins[skinIndex];
            ImGui::PushID(static_cast<int>(skinIndex));
            if (ImGui::TreeNode("Skin", "Skin[%zu] %s", skinIndex, skin.name.c_str())) {
                ImGui::Text("Skeleton Root Node: %d", skin.skeletonRootNode);
                ImGui::Text("Joint Count: %zu", skin.joints.size());
                const size_t maxDebugJoints = std::min<size_t>(skin.joints.size(), 32u);
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
        ImGui::Text("Mesh Count: %zu", meshCount);
        ImGui::Text("Primitive Count: %zu", primitiveCount);
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
        ImGui::Text("Skinned Mesh: %s", asset_->HasSkinnedMesh() ? "Yes" : "No");
        const MODELRENDERER::ModelRendererDebugStats& rendererStats = MODELRENDERER::GetDebugStats();
        ImGui::Text("Joint Palette Built: %s", rendererStats.builtPaletteCount > 0 ? "Yes" : "No");
        ImGui::Text("Skinned Nodes Rendered: %zu", rendererStats.skinnedNodeCount);
        ImGui::Text("Built Palettes: %zu", rendererStats.builtPaletteCount);
        ImGui::Text("Total Joint Matrices: %zu", rendererStats.totalJointMatrixCount);
        ImGui::Text("Last Skin Index: %d", rendererStats.lastSkinIndex);
        ImGui::Text("Last Palette Joint Count: %zu", rendererStats.lastPaletteJointCount);
        if (rendererStats.hasFirstJointMatrix) {
            const MATH::Mat4& m = rendererStats.firstJointMatrix;
            ImGui::Text("First Joint Matrix:");
            ImGui::Text("[%.3f %.3f %.3f %.3f]", m.m[0][0], m.m[1][0], m.m[2][0], m.m[3][0]);
            ImGui::Text("[%.3f %.3f %.3f %.3f]", m.m[0][1], m.m[1][1], m.m[2][1], m.m[3][1]);
            ImGui::Text("[%.3f %.3f %.3f %.3f]", m.m[0][2], m.m[1][2], m.m[2][2], m.m[3][2]);
            ImGui::Text("[%.3f %.3f %.3f %.3f]", m.m[0][3], m.m[1][3], m.m[2][3], m.m[3][3]);
        }
        ImGui::Text("Animation clips: %zu", asset_->animations.size());
        for (const AnimationClip& clip : asset_->animations) {
            ImGui::BulletText("%s (%.2fs)", clip.name.c_str(), clip.durationSec);
        }
        if (const Material* material = asset_->GetMaterial()) {
            const MATH::Vec4& color = material->GetBaseColor();
            ImGui::Text("BaseColor: (%.2f, %.2f, %.2f, %.2f)", color.x, color.y, color.z, color.w);
            const char* texturePath = material->GetBaseColorTexturePath().empty() ? "<none>" : material->GetBaseColorTexturePath().c_str();
            ImGui::Text("TexturePath: %s", texturePath);
            ImGui::Text("TextureHandle: %d (%s)",
                material->GetBaseColorTextureHandle(),
                material->HasBaseColorTexture() ? "Valid" : "Invalid");
        }
#endif
    }

} // namespace HIKARI
