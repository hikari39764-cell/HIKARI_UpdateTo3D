#include "HIKARI_ModelComponent.h"

#include "Assets/HIKARI_AssetTypes.h"
#include "Editor/HIKARI_IInspectorBuilder.h"
#include "Render3D/HIKARI_Material.h"
#include "Render3D/HIKARI_ModelAsset.h"
#include <cstring>

#if defined(_DEBUG)
#include "imgui.h"
#endif

namespace HIKARI {

    namespace {
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

    void ModelComponent::SetAsset(ModelAsset* asset) {
        asset_ = asset;
        if (asset_) {
            assetId_ = asset_->GetName();
        }
    }

    ModelAsset* ModelComponent::GetAsset() {
        return asset_;
    }

    const ModelAsset* ModelComponent::GetAsset() const {
        return asset_;
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
    }

    const std::string& ModelComponent::GetMaterialFxProfileId() const {
        return materialFxProfileId_;
    }

    void ModelComponent::Serialize(nlohmann::json& out) const {
        out["assetId"] = assetId_;
        out["visible"] = visible_;
        out["postGroupMask"] = postGroupMask_;
        out["materialFxProfileId"] = materialFxProfileId_;
    }

    void ModelComponent::Deserialize(const nlohmann::json& in) {
        assetId_ = in.value("assetId", assetId_);
        visible_ = in.value("visible", visible_);
        postGroupMask_ = in.value("postGroupMask", postGroupMask_);
        materialFxProfileId_ = in.value("materialFxProfileId", materialFxProfileId_);
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
        std::strncpy(profileBuffer, materialFxProfileId_.c_str(), sizeof(profileBuffer) - 1);
        if (ImGui::InputText("Material FX Profile", profileBuffer, sizeof(profileBuffer))) {
            materialFxProfileId_ = profileBuffer;
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
