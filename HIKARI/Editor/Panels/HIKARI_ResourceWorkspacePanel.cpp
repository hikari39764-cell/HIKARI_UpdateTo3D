#include "HIKARI_ResourceWorkspacePanel.h"

#include <algorithm>
#include <filesystem>
#include <string>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Style/HIKARI_EditorIconManager.h"
#include "Editor/Style/HIKARI_EditorWidgets.h"
#include "Scene/HIKARI_SceneDocument.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

    namespace {
#if defined(HIKARI_WITH_EDITOR)
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

        const char* ScopeLabel(AssetBrowserScope scope) {
            switch (scope) {
            case AssetBrowserScope::CurrentScene: return "Current Scene";
            case AssetBrowserScope::UnusedInScene: return "Unused";
            case AssetBrowserScope::Broken: return "Broken";
            case AssetBrowserScope::Textures: return "Textures";
            case AssetBrowserScope::Models: return "Models";
            case AssetBrowserScope::Scenes: return "Scenes";
            case AssetBrowserScope::Materials: return "Materials";
            case AssetBrowserScope::Skies: return "Skies";
            case AssetBrowserScope::Vfx: return "VFX";
            case AssetBrowserScope::Sequences: return "Sequences";
            case AssetBrowserScope::Project:
            default:
                return "Project";
            }
        }

        int ScopeCount(
            const AssetDatabase& assetDatabase,
            const AssetUsageSummary& usageSummary,
            AssetBrowserScope scope) {

            const int total = static_cast<int>(assetDatabase.CollectAll().size());
            const int used = static_cast<int>(usageSummary.usedGuids.size());
            switch (scope) {
            case AssetBrowserScope::CurrentScene:
                return used;
            case AssetBrowserScope::UnusedInScene:
                return (std::max)(0, total - used);
            case AssetBrowserScope::Broken:
                return CountBroken(assetDatabase) + static_cast<int>(usageSummary.missingReferences.size());
            case AssetBrowserScope::Textures:
                return CountByType(assetDatabase, AssetType::Texture);
            case AssetBrowserScope::Models:
                return CountByType(assetDatabase, AssetType::Model);
            case AssetBrowserScope::Scenes:
                return CountByType(assetDatabase, AssetType::Scene);
            case AssetBrowserScope::Materials:
                return CountByType(assetDatabase, AssetType::Material);
            case AssetBrowserScope::Skies:
                return CountByType(assetDatabase, AssetType::Sky);
            case AssetBrowserScope::Vfx:
                return CountByType(assetDatabase, AssetType::VfxEffect);
            case AssetBrowserScope::Sequences:
                return CountByType(assetDatabase, AssetType::Sequence);
            case AssetBrowserScope::Project:
            default:
                return total;
            }
        }

        void DrawScopeMenuItem(
            const AssetDatabase& assetDatabase,
            const AssetUsageSummary& usageSummary,
            const char* label,
            AssetBrowserScope scope,
            AssetBrowserScope& activeScope) {

            const std::string itemLabel =
                std::string(label) + "  " +
                std::to_string(ScopeCount(assetDatabase, usageSummary, scope));
            if (ImGui::MenuItem(itemLabel.c_str(), nullptr, activeScope == scope)) {
                activeScope = scope;
            }
        }

        void DrawScopeCombo(
            const AssetDatabase& assetDatabase,
            const AssetUsageSummary& usageSummary,
            AssetBrowserScope& activeScope) {

            const std::string preview =
                std::string(ScopeLabel(activeScope)) + "  " +
                std::to_string(ScopeCount(assetDatabase, usageSummary, activeScope));

            ImGui::SetNextItemWidth(190.0f);
            if (!ImGui::BeginCombo("##ResourceScope", preview.c_str())) {
                return;
            }

            ImGui::TextDisabled("Library");
            DrawScopeMenuItem(assetDatabase, usageSummary, "Project", AssetBrowserScope::Project, activeScope);
            DrawScopeMenuItem(assetDatabase, usageSummary, "Current Scene", AssetBrowserScope::CurrentScene, activeScope);
            DrawScopeMenuItem(assetDatabase, usageSummary, "Unused", AssetBrowserScope::UnusedInScene, activeScope);
            DrawScopeMenuItem(assetDatabase, usageSummary, "Broken", AssetBrowserScope::Broken, activeScope);

            ImGui::Separator();
            ImGui::TextDisabled("Types");
            DrawScopeMenuItem(assetDatabase, usageSummary, "Textures", AssetBrowserScope::Textures, activeScope);
            DrawScopeMenuItem(assetDatabase, usageSummary, "Models", AssetBrowserScope::Models, activeScope);
            DrawScopeMenuItem(assetDatabase, usageSummary, "Scenes", AssetBrowserScope::Scenes, activeScope);
            DrawScopeMenuItem(assetDatabase, usageSummary, "Materials", AssetBrowserScope::Materials, activeScope);
            DrawScopeMenuItem(assetDatabase, usageSummary, "Skies", AssetBrowserScope::Skies, activeScope);
            DrawScopeMenuItem(assetDatabase, usageSummary, "VFX", AssetBrowserScope::Vfx, activeScope);
            DrawScopeMenuItem(assetDatabase, usageSummary, "Sequences", AssetBrowserScope::Sequences, activeScope);

            if (!usageSummary.missingReferences.empty()) {
                ImGui::Separator();
                ImGui::TextColored(ImVec4(1.0f, 0.42f, 0.36f, 1.0f), "Missing References");
                for (const AssetMissingReference& missing : usageSummary.missingReferences) {
                    ImGui::TextDisabled("%s: %s", missing.role.c_str(), missing.assetId.c_str());
                }
            }

            ImGui::EndCombo();
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

            if (!record->artifactManifest.artifacts.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("Artifacts: %d", static_cast<int>(record->artifactManifest.artifacts.size()));
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
#if defined(HIKARI_WITH_EDITOR)
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
        DrawScopeCombo(assetDatabase, usageSummary, activeScope_);
        ImGui::SameLine();
        ImGui::TextDisabled("%d items", static_cast<int>(assetDatabase.CollectAll().size()));
        ImGui::SameLine();
        ImGui::TextDisabled("%d used in scene", static_cast<int>(usageSummary.usedGuids.size()));
        ImGui::SameLine();
        if (EDITOR::ActionButton(
                "Refresh",
                "ResourceRefresh",
                EDITOR::EditorButtonTone::Quiet,
                ImVec2(0.0f, 26.0f),
                "Refresh AssetDatabase and reload resources used by the current scene")) {
            assetDatabase.ScanAssets(true);
            refreshCurrentSceneResourcesRequested_ = true;
        }
        ImGui::SameLine();
        if (EDITOR::ActionButton(
                "Import Outdated",
                "ResourceImportOutdated",
                EDITOR::EditorButtonTone::Primary,
                ImVec2(0.0f, 26.0f))) {
            const AssetImportBatchResult result = assetDatabase.ImportAllOutdated();
            assetDatabase.ScanAssets(false);
            importMonitor_ = ResourceImportBatchMonitor{
                result.attempted,
                result.succeeded,
                result.failed,
                true,
                "Outdated assets"
            };
        }
        ImGui::SameLine();
        if (activeScope_ != AssetBrowserScope::Project) {
            ImGui::BeginDisabled();
        }
        if (EDITOR::ActionButton(
                "Import Current Folder",
                "ResourceImportCurrentFolder",
                EDITOR::EditorButtonTone::Neutral,
                ImVec2(0.0f, 26.0f),
                "Import outdated assets in the selected folder; recursive follows the browser toggle")) {
            const std::filesystem::path currentDirectory = assetBrowserPanel_.CurrentDirectory();
            const AssetImportBatchResult result =
                assetDatabase.ImportOutdatedInDirectory(currentDirectory, assetBrowserPanel_.IsRecursiveEnabled());
            assetDatabase.ScanAssets(false);
            importMonitor_ = ResourceImportBatchMonitor{
                result.attempted,
                result.succeeded,
                result.failed,
                true,
                "Current folder " + currentDirectory.generic_string()
            };
        }
        if (activeScope_ != AssetBrowserScope::Project) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (selectedRecord == nullptr) {
            ImGui::BeginDisabled();
        }
        if (EDITOR::ActionButton(
                "Import Dependencies",
                "ResourceImportDependencies",
                EDITOR::EditorButtonTone::Quiet,
                ImVec2(0.0f, 26.0f))) {
            const AssetImportBatchResult result = assetDatabase.ImportDependencies(selectedRecord->guid, false);
            assetDatabase.ScanAssets(false);
            importMonitor_ = ResourceImportBatchMonitor{
                result.attempted,
                result.succeeded,
                result.failed,
                true,
                "Selected dependencies"
            };
        }
        ImGui::SameLine();
        if (EDITOR::ActionButton(
                "Reimport Selected",
                "ResourceReimportSelected",
                EDITOR::EditorButtonTone::Neutral,
                ImVec2(0.0f, 26.0f))) {
            const bool ok = assetDatabase.ImportAsset(selectedRecord->guid);
            assetDatabase.ScanAssets(false);
            importMonitor_ = ResourceImportBatchMonitor{
                selectedRecord != nullptr ? 1 : 0,
                ok ? 1 : 0,
                ok ? 0 : 1,
                true,
                "Selected asset"
            };
        }
        if (selectedRecord == nullptr) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (EDITOR::ToggleButton(
                "Inspector",
                "ResourceInspectorToggle",
                showInspector_,
                ImVec2(0.0f, 26.0f),
                showInspector_ ? "Hide asset inspector" : "Show asset inspector")) {
            showInspector_ = !showInspector_;
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
        if (importMonitor_.hasResult) {
            const float progress = importMonitor_.attempted > 0
                ? static_cast<float>(importMonitor_.succeeded + importMonitor_.failed) /
                    static_cast<float>(importMonitor_.attempted)
                : 1.0f;
            ImGui::ProgressBar(progress, ImVec2(220.0f, 0.0f));
            ImGui::SameLine();
            ImGui::TextDisabled(
                "%s: %d attempted, %d ok, %d failed",
                importMonitor_.label.c_str(),
                importMonitor_.attempted,
                importMonitor_.succeeded,
                importMonitor_.failed);
            ImGui::Separator();
        }

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const bool wideLayout = available.x >= 980.0f;
        const bool showInspector = showInspector_ && selectedRecord != nullptr;
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

    std::string ResourceWorkspacePanel::ConsumeActivatedSequenceGuid() const {
        return assetBrowserPanel_.ConsumeActivatedSequenceGuid();
    }

    std::string ResourceWorkspacePanel::ConsumeActivatedModelCollisionGuid() const {
        return assetBrowserPanel_.ConsumeActivatedModelCollisionGuid();
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
