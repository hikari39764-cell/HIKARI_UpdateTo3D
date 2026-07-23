#include "HIKARI_ResourceWorkspacePanel.h"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Style/HIKARI_EditorGlyphs.h"
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

        ResourceImportBatchMonitor MakeImportMonitor(
            int attempted,
            int succeeded,
            int failed,
            std::string label) {
            const double visibilitySeconds = failed > 0
                ? 8.0
                : (attempted > 0 ? 3.5 : 2.0);
            return ResourceImportBatchMonitor{
                attempted,
                succeeded,
                failed,
                true,
                std::move(label),
                ImGui::GetTime() + visibilitySeconds
            };
        }

        ResourceImportBatchMonitor MakeImportMonitor(
            const AssetImportBatchResult& result,
            std::string label) {
            return MakeImportMonitor(
                result.attempted,
                result.succeeded,
                result.failed,
                std::move(label));
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
            ImGui::TextUnformatted("Background Asset Tasks");
            ImGui::SameLine();
            const std::vector<AssetTaskSnapshot> tasks =
                assetDatabase.GetAssetTaskService().
                    CollectSnapshots();
            ImGui::TextDisabled("%d recent", static_cast<int>(tasks.size()));
            if (!tasks.empty()) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear Completed")) {
                    assetDatabase.GetAssetTaskService().
                        ClearCompleted();
                }
            }
            ImGui::Separator();

            int drawnTasks = 0;
            for (const AssetTaskSnapshot& task : tasks) {
                if (drawnTasks >= 6) {
                    break;
                }
                ++drawnTasks;
                ImGui::PushID(static_cast<int>(task.id));
                ImGui::Text(
                    "%s  %s",
                    ToString(task.state),
                    task.label.c_str());
                ImGui::SameLine();
                ImGui::TextDisabled(
                    "%.2f s  |  %s",
                    task.elapsedSeconds,
                    task.progress.stage.c_str());
                if (!task.progress.currentItem.empty() &&
                    ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "%s",
                        task.progress.currentItem.c_str());
                }
                if (!task.resultMessage.empty() &&
                    IsTerminal(task.state)) {
                    ImGui::TextDisabled(
                        "%s",
                        task.resultMessage.c_str());
                }
                ImGui::PopID();
            }

            ImGui::Spacing();
            ImGui::SeparatorText("Selected Asset Import");
            if (selection.selectedAssetGuid.empty()) {
                ImGui::TextDisabled(
                    "Select an asset to inspect its latest import message");
            } else {
                const AssetRecord* record = assetDatabase.FindByGuid(
                    AssetGuid{ selection.selectedAssetGuid });
                if (!record) {
                    ImGui::TextDisabled(
                        "Selected asset is no longer available");
                    return;
                }

                ImGui::Text(
                    "%s | %s",
                    record->displayName.c_str(),
                    ToString(GetImportState(*record)));
                if (!record->lastImportMessage.empty()) {
                    ImGui::TextWrapped(
                        "%s",
                        record->lastImportMessage.c_str());
                } else {
                    ImGui::TextDisabled(
                        "No import report message yet");
                }

                if (!record->artifactManifest.artifacts.empty()) {
                    ImGui::SameLine();
                    ImGui::TextDisabled(
                        "Artifacts: %d",
                        static_cast<int>(
                            record->artifactManifest.artifacts.size()));
                }
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
        AssetImportBatchStatus completedBatch{};
        if (assetDatabase.ConsumeCompletedImportBatch(completedBatch)) {
            assetDatabase.ScanAssets(false);
            importMonitor_ = MakeImportMonitor(
                completedBatch.attempted,
                completedBatch.succeeded,
                completedBatch.failed,
                completedBatch.label);
        }
        const AssetImportBatchStatus importStatus =
            assetDatabase.GetQueuedImportStatus();
        const bool importActive = importStatus.active;

        DrawScopeCombo(assetDatabase, usageSummary, activeScope_);
        ImGui::SameLine();
        ImGui::TextDisabled(
            "%d assets  |  %d used",
            static_cast<int>(assetDatabase.CollectAll().size()),
            static_cast<int>(usageSummary.usedGuids.size()));
        ImGui::SameLine();
        if (EDITOR::IconToggleButton(
                EDITOR::EditorGlyph::Inspector,
                "ResourceInspectorToggle",
                showInspector_,
                ImVec2(28.0f, 28.0f),
                showInspector_ ? "Hide asset inspector" : "Show asset inspector")) {
            showInspector_ = !showInspector_;
        }
        ImGui::SameLine();
        if (EDITOR::IconToggleButton(
                EDITOR::EditorGlyph::Log,
                "ResourceImportLogToggle",
                showPreviewLog_,
                ImVec2(28.0f, 28.0f),
                showPreviewLog_ ? "Hide import log" : "Show import log")) {
            showPreviewLog_ = !showPreviewLog_;
        }
        const bool compactToolbar = ImGui::GetContentRegionAvail().x < 900.0f;

        if (importActive) {
            ImGui::BeginDisabled();
        }
        if (EDITOR::IconButton(
                EDITOR::EditorGlyph::Refresh,
                "ResourceRefresh",
                EDITOR::EditorButtonTone::Quiet,
                ImVec2(28.0f, 28.0f),
                "Refresh AssetDatabase and reload resources used by the current scene")) {
            assetDatabase.ScanAssets(true);
            refreshCurrentSceneResourcesRequested_ = true;
        }
        if (importActive) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (importActive) {
            ImGui::BeginDisabled();
        }
        if (EDITOR::IconTextButton(
                EDITOR::EditorGlyph::Import,
                "Import Outdated",
                "ResourceImportOutdated",
                EDITOR::EditorButtonTone::Primary,
                ImVec2(0.0f, 28.0f),
                "Import every asset whose source is newer than its artifact")) {
            (void)assetDatabase.QueueImportAllOutdated();
        }
        if (importActive) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (activeScope_ != AssetBrowserScope::Project ||
            importActive) {
            ImGui::BeginDisabled();
        }
        const bool importCurrentFolder = compactToolbar
            ? EDITOR::IconButton(
                EDITOR::EditorGlyph::Folder,
                "ResourceImportCurrentFolder",
                EDITOR::EditorButtonTone::Neutral,
                ImVec2(28.0f, 28.0f),
                "Import outdated assets in the selected folder; recursive follows the browser toggle")
            : EDITOR::IconTextButton(
                EDITOR::EditorGlyph::Folder,
                "Current Folder",
                "ResourceImportCurrentFolder",
                EDITOR::EditorButtonTone::Neutral,
                ImVec2(0.0f, 28.0f),
                "Import outdated assets in the selected folder; recursive follows the browser toggle");
        if (importCurrentFolder) {
            const std::filesystem::path currentDirectory = assetBrowserPanel_.CurrentDirectory();
            (void)assetDatabase.QueueImportOutdatedInDirectory(
                currentDirectory,
                assetBrowserPanel_.IsRecursiveEnabled());
        }
        if (activeScope_ != AssetBrowserScope::Project ||
            importActive) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (selectedRecord == nullptr || importActive) {
            ImGui::BeginDisabled();
        }
        const bool importDependencies = compactToolbar
            ? EDITOR::IconButton(
                EDITOR::EditorGlyph::Dependency,
                "ResourceImportDependencies",
                EDITOR::EditorButtonTone::Quiet,
                ImVec2(28.0f, 28.0f),
                "Import dependencies of the selected asset")
            : EDITOR::IconTextButton(
                EDITOR::EditorGlyph::Dependency,
                "Dependencies",
                "ResourceImportDependencies",
                EDITOR::EditorButtonTone::Quiet,
                ImVec2(0.0f, 28.0f),
                "Import dependencies of the selected asset");
        if (importDependencies) {
            (void)assetDatabase.QueueImportDependencies(
                selectedRecord->guid,
                false);
        }
        ImGui::SameLine();
        const bool reimportSelected = compactToolbar
            ? EDITOR::IconButton(
                EDITOR::EditorGlyph::Reimport,
                "ResourceReimportSelected",
                EDITOR::EditorButtonTone::Neutral,
                ImVec2(28.0f, 28.0f),
                "Reimport the selected asset")
            : EDITOR::IconTextButton(
                EDITOR::EditorGlyph::Reimport,
                "Reimport",
                "ResourceReimportSelected",
                EDITOR::EditorButtonTone::Neutral,
                ImVec2(0.0f, 28.0f),
                "Reimport the selected asset");
        if (reimportSelected) {
            (void)assetDatabase.QueueImportAssets(
                { selectedRecord->guid },
                "Selected asset");
        }
        if (selectedRecord == nullptr || importActive) {
            ImGui::EndDisabled();
        }
        if (importActive) {
            float activeProgress = 0.0f;
            std::string activeStage = "Waiting for worker";
            std::string activeItem{};
            for (AssetTaskId taskId :
                 importStatus.activeTaskIds) {
                const std::optional<AssetTaskSnapshot> snapshot =
                    assetDatabase.GetAssetTaskService().
                        FindSnapshot(taskId);
                if (!snapshot) {
                    continue;
                }
                activeProgress += snapshot->progress.determinate
                    ? snapshot->progress.normalized
                    : 0.0f;
                if (activeItem.empty() &&
                    snapshot->state != AssetTaskState::Queued) {
                    activeStage = snapshot->progress.stage;
                    activeItem = snapshot->progress.currentItem;
                }
            }
            const float overallProgress = importStatus.total > 0
                ? (std::clamp)(
                    (static_cast<float>(importStatus.finished) +
                        activeProgress) /
                        static_cast<float>(importStatus.total),
                    0.0f,
                    1.0f)
                : 0.0f;
            ImGui::SetNextItemWidth(
                (std::min)(360.0f, ImGui::GetContentRegionAvail().x));
            ImGui::ProgressBar(
                overallProgress,
                ImVec2(0.0f, 22.0f),
                activeStage.c_str());
            if (!activeItem.empty() &&
                ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", activeItem.c_str());
            }
            ImGui::SameLine();
            ImGui::TextDisabled(
                "%d / %d  |  %d ok  |  %d failed",
                importStatus.finished,
                importStatus.total,
                importStatus.succeeded,
                importStatus.failed);
            ImGui::SameLine();
            ImGui::BeginDisabled(
                importStatus.cancellationRequested);
            if (ImGui::SmallButton(
                    importStatus.cancellationRequested
                        ? "Cancel Requested"
                        : "Cancel")) {
                (void)assetDatabase.RequestCancelQueuedImport();
            }
            ImGui::EndDisabled();
        } else if (importMonitor_.hasResult &&
            ImGui::GetTime() <= importMonitor_.visibleUntilSeconds) {
            const bool failed = importMonitor_.failed > 0;
            const char* statusLabel = failed
                ? "Import failed"
                : (importMonitor_.attempted > 0
                    ? "Import complete"
                    : "Up to date");
            EDITOR::StatusBadge(
                statusLabel,
                failed
                    ? EDITOR::EditorStatusTone::Error
                    : EDITOR::EditorStatusTone::Ready);
            ImGui::SameLine();
            ImGui::TextDisabled(
                "%s: %d attempted, %d ok, %d failed",
                importMonitor_.label.c_str(),
                importMonitor_.attempted,
                importMonitor_.succeeded,
                importMonitor_.failed);
        } else {
            importMonitor_.hasResult = false;
        }
        ImGui::Separator();

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

    std::string ResourceWorkspacePanel::
        ConsumeActivatedAnimationStateMachineGuid() const {
        return assetBrowserPanel_.
            ConsumeActivatedAnimationStateMachineGuid();
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
