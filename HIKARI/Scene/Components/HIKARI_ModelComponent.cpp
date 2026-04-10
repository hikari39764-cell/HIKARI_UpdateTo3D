#include "HIKARI_ModelComponent.h"
#include "Render3D/HIKARI_ModelAsset.h"
#include "imgui.h"

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

    void ModelComponent::RenderImGui() {
        ImGui::Checkbox("Visible", &visible_);
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
        }
    }

} // namespace HIKARI
