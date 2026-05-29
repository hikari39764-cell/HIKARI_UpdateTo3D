#include "HIKARI_ResourceWorkspacePanel.h"

#include <algorithm>
#include <filesystem>
#include <string>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Editor/HIKARI_EditorSelection.h"
#include "Editor/Style/HIKARI_EditorIconManager.h"
#include "Scene/HIKARI_SceneDocument.h"

#if defined(_DEBUG)
#include "imgui.h"
#endif

namespace HIKARI {

    namespace {
#if defined(_DEBUG)
        bool IsBrokenRecord(const AssetRecord& record) {
            const AssetImportState state = GetImportState(record);
            return state == AssetImportState::MissingSource ||
                state == AssetImportState::MissingMeta ||
                state == AssetImportState::MissingArtifact ||
                state == AssetImportState::UnknownImporter ||
                state == AssetImportState::DuplicateGuid ||
                state == AssetImportState::ImportFailed;
        }

        int CountByType(const AssetDatabase& assetDatabase, AssetType type) {
            return static_cast<int>(assetDatabase.CollectByType(type).size());
        }

        int CountBroken(const AssetDatabase& assetDatabase) {
            int count = 0;
            for (const AssetRecord* record : assetDatabase.CollectAll()) {
                if (record && IsBrokenRecord(*record)) {
                    ++count;
                }
            }
            return count;
        }

        void DrawScopeButton(
            const char* label,
            int count,
            AssetBrowserScope scope,
            AssetBrowserScope& activeScope) {

            const bool selected = activeScope == scope;
            if (selected) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.32f, 0.48f, 1.0f));
            }

            const std::string text = std::string(label) + "  " + std::to_string(count);
            if (ImGui::Button(text.c_str(), ImVec2(-1.0f, 30.0f))) {
                activeScope = scope;
            }

            if (selected) {
                ImGui::PopStyleColor();
            }
        }

        void DrawScopeRail(
            AssetDatabase& assetDatabase,
            const AssetUsageSummary& usageSummary,
            AssetBrowserScope& activeScope) {

            const int total = static_cast<int>(assetDatabase.CollectAll().size());
            const int used = static_cast<int>(usageSummary.usedGuids.size());
            const int unused = (std::max)(0, total - used);

            ImGui::TextUnformatted("Library");
            ImGui::Separator();
            DrawScopeButton("Project", total, AssetBrowserScope::Project, activeScope);
            DrawScopeButton("Current Scene", used, AssetBrowserScope::CurrentScene, activeScope);
            DrawScopeButton("Unused", unused, AssetBrowserScope::UnusedInScene, activeScope);
            DrawScopeButton("Broken", CountBroken(assetDatabase) + static_cast<int>(usageSummary.missingReferences.size()), AssetBrowserScope::Broken, activeScope);

            ImGui::Spacing();
            ImGui::TextUnformatted("Types");
            ImGui::Separator();
            DrawScopeButton("Textures", CountByType(assetDatabase, AssetType::Texture), AssetBrowserScope::Textures, activeScope);
            DrawScopeButton("Models", CountByType(assetDatabase, AssetType::Model), AssetBrowserScope::Models, activeScope);
            DrawScopeButton("Scenes", CountByType(assetDatabase, AssetType::Scene), AssetBrowserScope::Scenes, activeScope);
            DrawScopeButton("Materials", CountByType(assetDatabase, AssetType::Material), AssetBrowserScope::Materials, activeScope);
            DrawScopeButton("Skies", CountByType(assetDatabase, AssetType::Sky), AssetBrowserScope::Skies, activeScope);
            DrawScopeButton("VFX", CountByType(assetDatabase, AssetType::VfxEffect), AssetBrowserScope::Vfx, activeScope);

            if (!usageSummary.missingReferences.empty()) {
                ImGui::Spacing();
                ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.36f, 1.0f), "Missing References");
                for (const AssetMissingReference& missing : usageSummary.missingReferences) {
                    ImGui::TextWrapped("%s: %s", missing.role.c_str(), missing.assetId.c_str());
                    ImGui::TextDisabled("%s", missing.owner.c_str());
                }
            }
        }

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

    void ResourceWorkspacePanel::Draw(
        AssetDatabase& assetDatabase,
        AssetRegistry& assetRegistry,
        const SceneDocument& sceneDocument,
        EditorSelection& selection,
        const ResourceWorkspaceContext& context) const {
#if defined(_DEBUG)
        if (!ImGui::Begin("Resource Workspace")) {
            ImGui::End();
            return;
        }

        AssetUsageSummary usageSummary = AnalyzeAssetUsage(sceneDocument, assetDatabase);
        const AssetBrowserContext browserContext{
            context.currentSceneGuid,
            context.startupSceneGuid,
            context.currentSceneDirty
        };

        const AssetRecord* selectedRecord = selection.selectedAssetGuid.empty()
            ? nullptr
            : assetDatabase.FindByGuid(AssetGuid{ selection.selectedAssetGuid });

        ImGui::TextUnformatted("Resources");
        ImGui::SameLine();
        ImGui::TextDisabled("%d items", static_cast<int>(assetDatabase.CollectAll().size()));
        ImGui::SameLine();
        ImGui::TextDisabled("%d used in scene", static_cast<int>(usageSummary.usedGuids.size()));
        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh")) {
            assetDatabase.ScanAssets(true);
            refreshCurrentSceneResourcesRequested_ = true;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Refreshes AssetDatabase and reloads resources used by the current scene.");
        }
        ImGui::SameLine();
        if (EDITOR::EditorIconManager::IconButton(
            EDITOR::EditorIconKind::Settings,
            "ResourceImportLogToggle",
            ImVec2(24.0f, 24.0f),
            showPreviewLog_,
            showPreviewLog_ ? "Hide import log" : "Show import log")) {
            showPreviewLog_ = !showPreviewLog_;
        }
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
                const float railWidth = 176.0f;
                if (ImGui::BeginChild("##ResourceWorkspaceScopeRail", ImVec2(railWidth, 0.0f), true)) {
                    DrawScopeRail(assetDatabase, usageSummary, activeScope_);
                }
                ImGui::EndChild();
                ImGui::SameLine();

                const float browserWidth = showInspector ? -inspectorWidth - 8.0f : 0.0f;
                if (ImGui::BeginChild("##ResourceWorkspaceBrowser", ImVec2(browserWidth, 0.0f), false)) {
                    assetBrowserPanel_.DrawContents(assetDatabase, selection, &usageSummary, activeScope_, &browserContext);
                }
                ImGui::EndChild();

                if (showInspector) {
                    ImGui::SameLine();

                    if (ImGui::BeginChild("##ResourceWorkspaceInspector", ImVec2(0.0f, 0.0f), true)) {
                        assetInspectorPanel_.Draw(assetDatabase, assetRegistry, selection);
                    }
                    ImGui::EndChild();
                }
            } else if (ImGui::BeginTabBar("ResourceWorkspaceCompactTabs", ImGuiTabBarFlags_FittingPolicyScroll)) {
                if (ImGui::BeginTabItem("Browser")) {
                    assetBrowserPanel_.DrawContents(assetDatabase, selection, &usageSummary, activeScope_, &browserContext);
                    ImGui::EndTabItem();
                }
                if (ImGui::BeginTabItem("Categories")) {
                    DrawScopeRail(assetDatabase, usageSummary, activeScope_);
                    ImGui::EndTabItem();
                }
                if (showInspector && ImGui::BeginTabItem("Inspector")) {
                    assetInspectorPanel_.Draw(assetDatabase, assetRegistry, selection);
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
        (void)assetRegistry;
        (void)sceneDocument;
        (void)selection;
        (void)context;
#endif
    }

    std::string ResourceWorkspacePanel::ConsumeActivatedSceneGuid() const {
        return assetBrowserPanel_.ConsumeActivatedSceneGuid();
    }

    std::string ResourceWorkspacePanel::ConsumeSaveSceneAsGuid() const {
        return assetBrowserPanel_.ConsumeSaveSceneAsGuid();
    }

    std::string ResourceWorkspacePanel::ConsumeRefreshRuntimeAssetGuid() const {
        return assetBrowserPanel_.ConsumeRefreshRuntimeAssetGuid();
    }

    std::string ResourceWorkspacePanel::ConsumeReimportAndRefreshRuntimeAssetGuid() const {
        return assetBrowserPanel_.ConsumeReimportAndRefreshRuntimeAssetGuid();
    }

    bool ResourceWorkspacePanel::ConsumeApplyRuntimeMaterialRequest(
        AssetGuid& outGuid,
        PbrMaterialAssetData& outData) const {

        return assetInspectorPanel_.ConsumeApplyRuntimeMaterialRequest(outGuid, outData);
    }

    std::string ResourceWorkspacePanel::ConsumeRefreshRuntimeMaterialGuid() const {
        return assetInspectorPanel_.ConsumeRefreshRuntimeMaterialGuid();
    }

    bool ResourceWorkspacePanel::ConsumeRefreshCurrentSceneResourcesRequested() const {
        const bool requested = refreshCurrentSceneResourcesRequested_;
        refreshCurrentSceneResourcesRequested_ = false;
        return requested;
    }

} // namespace HIKARI
