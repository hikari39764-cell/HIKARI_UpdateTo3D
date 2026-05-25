#include "HIKARI_ResourceWorkspacePanel.h"

#include <algorithm>
#include <filesystem>
#include <string>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Editor/HIKARI_EditorSelection.h"

#if defined(_DEBUG)
#include "imgui.h"
#endif

namespace HIKARI {

    namespace {
#if defined(_DEBUG)
        void DrawPreviewAndImportLog(AssetDatabase& assetDatabase, EditorSelection& selection) {
            ImGui::TextUnformatted("Import Log / Preview");
            ImGui::Separator();

            if (selection.selectedAssetGuid.empty()) {
                ImGui::TextDisabled("Select an asset to inspect its latest import message");
                return;
            }

            const AssetRecord* record = assetDatabase.FindByGuid(AssetGuid{ selection.selectedAssetGuid });
            if (!record) {
                ImGui::TextDisabled("Selected asset is no longer available");
                return;
            }

            ImGui::Text("%s | %s", record->displayName.c_str(), ToString(GetImportState(*record)));
            if (!record->lastImportMessage.empty()) {
                ImGui::TextWrapped("%s", record->lastImportMessage.c_str());
            } else {
                ImGui::TextDisabled("No import report message yet");
            }

            if (!record->meta.artifacts.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("Artifacts: %d", static_cast<int>(record->meta.artifacts.size()));
            }
        }
#endif
    }

    void ResourceWorkspacePanel::Draw(AssetDatabase& assetDatabase, EditorSelection& selection) const {
#if defined(_DEBUG)
        if (!ImGui::Begin("Resource Workspace")) {
            ImGui::End();
            return;
        }

        const AssetRecord* selectedRecord = selection.selectedAssetGuid.empty()
            ? nullptr
            : assetDatabase.FindByGuid(AssetGuid{ selection.selectedAssetGuid });

        ImGui::TextUnformatted("Project Assets");
        ImGui::SameLine();
        ImGui::TextDisabled("%d items", static_cast<int>(assetDatabase.CollectAll().size()));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(112.0f);
        ImGui::Checkbox("Import Log", &showPreviewLog_);
        ImGui::Separator();

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const bool wideLayout = available.x >= 980.0f;
        const bool showInspector = selectedRecord != nullptr;
        const bool canShowBottom = showPreviewLog_ && available.y >= 520.0f;
        const float bottomHeight = canShowBottom
            ? (std::min)(170.0f, (std::max)(112.0f, available.y * 0.22f))
            : 0.0f;
        const float inspectorWidth = (wideLayout && showInspector)
            ? (std::min)(390.0f, (std::max)(320.0f, available.x * 0.30f))
            : 0.0f;

        if (ImGui::BeginChild("##ResourceWorkspaceMain", ImVec2(0.0f, -bottomHeight), false)) {
            if (wideLayout) {
                const float browserWidth = showInspector ? -inspectorWidth - 8.0f : 0.0f;
                if (ImGui::BeginChild("##ResourceWorkspaceBrowser", ImVec2(browserWidth, 0.0f), false)) {
                    assetBrowserPanel_.DrawContents(assetDatabase, selection);
                }
                ImGui::EndChild();

                if (showInspector) {
                    ImGui::SameLine();

                    if (ImGui::BeginChild("##ResourceWorkspaceInspector", ImVec2(0.0f, 0.0f), true)) {
                        assetInspectorPanel_.Draw(assetDatabase, selection);
                    }
                    ImGui::EndChild();
                }
            } else if (ImGui::BeginTabBar("ResourceWorkspaceCompactTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
                if (ImGui::BeginTabItem("Browser")) {
                    assetBrowserPanel_.DrawContents(assetDatabase, selection);
                    ImGui::EndTabItem();
                }
                if (showInspector && ImGui::BeginTabItem("Inspector")) {
                    assetInspectorPanel_.Draw(assetDatabase, selection);
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }
        }
        ImGui::EndChild();

        if (canShowBottom && ImGui::BeginChild("##ResourceWorkspaceBottom", ImVec2(0.0f, 0.0f), true)) {
            DrawPreviewAndImportLog(assetDatabase, selection);
            ImGui::EndChild();
        }

        ImGui::End();
#else
        (void)assetDatabase;
        (void)selection;
#endif
    }

} // namespace HIKARI
