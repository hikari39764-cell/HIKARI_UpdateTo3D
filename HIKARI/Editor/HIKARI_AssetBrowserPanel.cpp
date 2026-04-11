#include "HIKARI_AssetBrowserPanel.h"
#include <string>
#include "HIKARI_EditorSelection.h"
#include "Render3D/HIKARI_Material.h"
#include "Render3D/HIKARI_ModelManager.h"
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

        const char* GetSourceType(const std::string& sourcePath) {
            if (sourcePath == "builtin:cube") {
                return "builtin";
            }
            const size_t dot = sourcePath.find_last_of('.');
            if (dot == std::string::npos) {
                return "unknown";
            }
            return sourcePath.c_str() + dot + 1;
        }
    }

    void AssetBrowserPanel::Draw(ModelManager& modelManager, EditorSelection& selection) const {
#if defined(_DEBUG)
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
            ImGui::Text("  Source: %s", assetPtr->GetSourcePath().c_str());
            ImGui::Text("  Type: %s | State: %s | Mesh: %s",
                GetSourceType(assetPtr->GetSourcePath()),
                ToStateText(assetPtr->GetState()),
                assetPtr->GetMesh() ? "Yes" : "No");
            if (const Material* material = assetPtr->GetMaterial()) {
                const bool hasTexture = material->HasBaseColorTexture();
                const char* texturePath = material->GetBaseColorTexturePath().empty() ? "<none>" : material->GetBaseColorTexturePath().c_str();
                ImGui::Text("  Texture Path: %s", texturePath);
                ImGui::Text("  Texture: %s (handle=%d)", hasTexture ? "Loaded" : "Not Loaded", material->GetBaseColorTextureHandle());
            } else {
                ImGui::TextUnformatted("  Texture Path: <no material>");
            }
        }

        ImGui::End();
#else
        (void)modelManager;
        (void)selection;
#endif
    }

} // namespace HIKARI
