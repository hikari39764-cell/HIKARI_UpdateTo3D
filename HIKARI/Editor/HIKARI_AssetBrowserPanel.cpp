#include "HIKARI_AssetBrowserPanel.h"
#include "HIKARI_EditorSelection.h"
#include "Render3D/HIKARI_ModelManager.h"
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

    void AssetBrowserPanel::Draw(ModelManager& modelManager, EditorSelection& selection) const {
        if (!ImGui::Begin("Asset Browser")) {
            ImGui::End();
            return;
        }

        for (const auto& asset : modelManager.GetAssets()) {
            ModelAsset* assetPtr = asset.get();
            const bool isSelected = (selection.selectedAsset == assetPtr);
            if (ImGui::Selectable(assetPtr->GetName().c_str(), isSelected)) {
                selection.selectedAsset = assetPtr;
            }
            ImGui::SameLine(240.0f);
            ImGui::Text("%s | %s", assetPtr->GetSourcePath().c_str(), ToStateText(assetPtr->GetState()));
        }

        ImGui::End();
    }

} // namespace HIKARI
