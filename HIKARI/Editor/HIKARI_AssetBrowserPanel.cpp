#include "HIKARI_AssetBrowserPanel.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>

#include <Windows.h>
#include <Shellapi.h>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/Legacy/HIKARI_LegacyAssetJsonMigrator.h"
#include "HIKARI_EditorSelection.h"
#include "Render3D/Core/HIKARI_Material.h"
#include "Render3D/Core/HIKARI_ModelManager.h"

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

        const char* ToAssetTypeText(AssetType type) {
            switch (type) {
            case AssetType::Model: return "Model";
            case AssetType::Sky: return "Sky";
            case AssetType::Texture: return "Texture";
            case AssetType::Material: return "Material";
            case AssetType::Animation: return "Animation";
            case AssetType::Particle: return "Particle";
            case AssetType::VfxEffect: return "Vfx";
            case AssetType::Unknown:
            default: return "Unknown";
            }
        }

        const char* ToAssetIcon(AssetType type) {
            switch (type) {
            case AssetType::Model: return "[M]";
            case AssetType::Sky: return "[S]";
            case AssetType::Texture: return "[T]";
            case AssetType::Material: return "[Mat]";
            case AssetType::VfxEffect: return "[V]";
            default: return "[?]";
            }
        }

        std::string ToLowerCopy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        AssetType TypeFromFilterIndex(int index) {
            switch (index) {
            case 1: return AssetType::Texture;
            case 2: return AssetType::Model;
            case 3: return AssetType::Sky;
            case 4: return AssetType::Material;
            case 5: return AssetType::VfxEffect;
            default: return AssetType::Unknown;
            }
        }

        bool MatchesTypeFilter(const AssetRecord& record, int typeFilter) {
            if (typeFilter == 0) {
                return true;
            }
            return record.type == TypeFromFilterIndex(typeFilter);
        }

        bool MatchesStateFilter(const AssetRecord& record, int stateFilter) {
            if (stateFilter == 0) {
                return true;
            }

            const AssetImportState state = GetImportState(record);
            switch (stateFilter) {
            case 1:
                return state == AssetImportState::Imported;
            case 2:
                return state == AssetImportState::Outdated;
            case 3:
                return state == AssetImportState::MissingSource ||
                    state == AssetImportState::MissingMeta ||
                    state == AssetImportState::MissingArtifact;
            case 4:
                return state == AssetImportState::UnknownImporter ||
                    state == AssetImportState::DuplicateGuid ||
                    state == AssetImportState::ImportFailed;
            case 5:
                return state == AssetImportState::MetaOnly;
            default:
                return true;
            }
        }

        bool MatchesSearch(const AssetRecord& record, const char* searchText) {
            if (!searchText || searchText[0] == '\0') {
                return true;
            }

            const std::string needle = ToLowerCopy(searchText);
            const std::string haystack = ToLowerCopy(
                record.displayName + " " +
                record.guid.value + " " +
                record.sourcePath.generic_string() + " " +
                record.meta.importerId);
            return haystack.find(needle) != std::string::npos;
        }

#if defined(_DEBUG)
        ImVec4 StateColor(AssetImportState state) {
            switch (state) {
            case AssetImportState::Imported:
                return ImVec4(0.62f, 0.86f, 0.66f, 1.0f);
            case AssetImportState::Outdated:
                return ImVec4(0.95f, 0.78f, 0.34f, 1.0f);
            case AssetImportState::MissingArtifact:
                return ImVec4(1.0f, 0.58f, 0.28f, 1.0f);
            case AssetImportState::MissingSource:
            case AssetImportState::UnknownImporter:
            case AssetImportState::DuplicateGuid:
            case AssetImportState::ImportFailed:
                return ImVec4(1.0f, 0.36f, 0.36f, 1.0f);
            case AssetImportState::MetaOnly:
                return ImVec4(0.62f, 0.66f, 0.72f, 1.0f);
            case AssetImportState::MissingMeta:
            case AssetImportState::Unknown:
            default:
                return ImVec4(0.78f, 0.78f, 0.78f, 1.0f);
            }
        }
#endif

        void ShowInExplorer(const std::filesystem::path& path) {
            const std::wstring param = L"/select,\"" + path.wstring() + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", param.c_str(), nullptr, SW_SHOWNORMAL);
        }

        std::string FirstArtifactPath(const AssetRecord& record) {
            for (const AssetArtifactDesc& artifact : record.meta.artifacts) {
                if (!artifact.path.empty()) {
                    return artifact.path;
                }
            }
            return {};
        }

        std::filesystem::path MakeUniqueFolderPath(const std::filesystem::path& parentDirectory) {
            std::filesystem::path candidate = parentDirectory / "New Folder";
            std::error_code ec{};
            if (!std::filesystem::exists(candidate, ec)) {
                return candidate;
            }

            for (int i = 2; i < 1000; ++i) {
                candidate = parentDirectory / ("New Folder " + std::to_string(i));
                ec.clear();
                if (!std::filesystem::exists(candidate, ec)) {
                    return candidate;
                }
            }

            return parentDirectory / "New Folder 999";
        }

#if defined(_DEBUG)
        void SelectRecord(const AssetRecord& record, EditorSelection& selection) {
            selection.selectedAssetGuid = record.guid.value;
            selection.selectedAssetPath = record.sourcePath.generic_string();
            selection.selectedAsset = nullptr;
        }

        void DrawRecordContextMenu(
            AssetDatabase& assetDatabase,
            const AssetRecord& record,
            EditorSelection& selection,
            std::string& lastOperationMessage) {

            if (ImGui::MenuItem("Reimport")) {
                SelectRecord(record, selection);
                const bool ok = assetDatabase.ImportAsset(record.guid);
                lastOperationMessage = ok ? "Reimport succeeded" : "Reimport failed";
            }
            if (ImGui::MenuItem("Reimport Dependencies")) {
                lastOperationMessage = "Dependency reimport is reserved for dependency graph import";
            }
            if (ImGui::MenuItem("Show in Explorer")) {
                ShowInExplorer(assetDatabase.GetProjectRoot() / record.sourcePath);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Copy GUID")) {
                ImGui::SetClipboardText(record.guid.value.c_str());
                lastOperationMessage = "Asset GUID copied";
            }
            if (ImGui::MenuItem("Copy Source Path")) {
                const std::string path = record.sourcePath.generic_string();
                ImGui::SetClipboardText(path.c_str());
                lastOperationMessage = "Source path copied";
            }
            const std::string artifactPath = FirstArtifactPath(record);
            if (ImGui::MenuItem("Copy Artifact Path", nullptr, false, !artifactPath.empty())) {
                ImGui::SetClipboardText(artifactPath.c_str());
                lastOperationMessage = "Artifact path copied";
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Regenerate Meta")) {
                SelectRecord(record, selection);
                const bool ok = assetDatabase.RegenerateMeta(record.sourcePath);
                lastOperationMessage = ok ? "Meta regenerated" : "Meta regeneration failed";
            }
        }

        void DrawRecordList(
            AssetDatabase& assetDatabase,
            const std::vector<const AssetRecord*>& records,
            EditorSelection& selection,
            std::string& lastOperationMessage) {

            if (!ImGui::BeginTable(
                "AssetBrowserTable",
                5,
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_BordersInnerV |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_ScrollY)) {
                return;
            }

            ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Importer", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            for (const AssetRecord* record : records) {
                if (!record) {
                    continue;
                }

                ImGui::PushID(record->guid.IsValid()
                    ? record->guid.value.c_str()
                    : record->sourcePath.generic_string().c_str());
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const bool isSelected = selection.selectedAssetGuid == record->guid.value;
                const std::string label =
                    std::string(ToAssetIcon(record->type)) + " " +
                    record->displayName + "##" +
                    record->sourcePath.generic_string();
                if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                    SelectRecord(*record, selection);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && record->type == AssetType::Model) {
                        lastOperationMessage = "Model selected: " + record->displayName;
                    }
                }

                if (ImGui::BeginPopupContextItem()) {
                    DrawRecordContextMenu(assetDatabase, *record, selection, lastOperationMessage);
                    ImGui::EndPopup();
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(ToAssetTypeText(record->type));
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(record->meta.importerId.empty() ? "<none>" : record->meta.importerId.c_str());
                ImGui::TableSetColumnIndex(3);
                const AssetImportState state = GetImportState(*record);
                ImGui::TextColored(StateColor(state), "%s", ToString(state));
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(record->sourcePath.generic_string().c_str());
                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        void DrawRecordGrid(
            AssetDatabase& assetDatabase,
            const std::vector<const AssetRecord*>& records,
            EditorSelection& selection,
            std::string& lastOperationMessage) {

            const float cardWidth = 172.0f;
            const float spacing = ImGui::GetStyle().ItemSpacing.x;
            const float availableWidth = (std::max)(cardWidth, ImGui::GetContentRegionAvail().x);
            const int columns = (std::max)(1, static_cast<int>(availableWidth / (cardWidth + spacing)));

            if (!ImGui::BeginTable("AssetBrowserGrid", columns, ImGuiTableFlags_SizingFixedFit)) {
                return;
            }

            int column = 0;
            for (const AssetRecord* record : records) {
                if (!record) {
                    continue;
                }

                if (column == 0) {
                    ImGui::TableNextRow();
                }
                ImGui::TableSetColumnIndex(column);

                ImGui::PushID(record->guid.IsValid()
                    ? record->guid.value.c_str()
                    : record->sourcePath.generic_string().c_str());

                const bool isSelected = selection.selectedAssetGuid == record->guid.value;
                if (isSelected) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.19f, 0.34f, 0.52f, 1.0f));
                }

                std::string buttonLabel =
                    std::string(ToAssetIcon(record->type)) + "\n" +
                    record->displayName + "\n" +
                    ToString(GetImportState(*record));
                if (ImGui::Button(buttonLabel.c_str(), ImVec2(cardWidth, 78.0f))) {
                    SelectRecord(*record, selection);
                }
                if (isSelected) {
                    ImGui::PopStyleColor();
                }
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    SelectRecord(*record, selection);
                    if (record->type == AssetType::Model) {
                        lastOperationMessage = "Model selected: " + record->displayName;
                    }
                }
                if (ImGui::BeginPopupContextItem()) {
                    DrawRecordContextMenu(assetDatabase, *record, selection, lastOperationMessage);
                    ImGui::EndPopup();
                }

                ImGui::PopID();
                column = (column + 1) % columns;
            }

            ImGui::EndTable();
        }
#endif
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

    void AssetBrowserPanel::Draw(AssetDatabase& assetDatabase, EditorSelection& selection) const {
#if defined(_DEBUG)
        if (!ImGui::Begin("Asset Browser")) {
            ImGui::End();
            return;
        }
        DrawContents(assetDatabase, selection);
        ImGui::End();
#else
        (void)assetDatabase;
        (void)selection;
#endif
    }

    void AssetBrowserPanel::DrawContents(AssetDatabase& assetDatabase, EditorSelection& selection) const {
#if defined(_DEBUG)
        if (currentDirectory_.empty()) {
            currentDirectory_ = "Assets";
        }

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
        ImGui::TextUnformatted("Project Assets");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", currentDirectory_.generic_string().c_str());
        ImGui::Separator();

        if (ImGui::Button("Refresh")) {
            const bool ok = assetDatabase.ScanAssets(true);
            lastOperationMessage_ = ok ? "AssetDatabase refreshed" : "AssetDatabase refresh failed";
        }
        ImGui::SameLine();
        if (ImGui::Button("Import All Outdated")) {
            const AssetImportBatchResult result = assetDatabase.ImportAllOutdated();
            lastOperationMessage_ =
                "Imported " + std::to_string(result.succeeded) +
                " assets, failed " + std::to_string(result.failed);
        }
        ImGui::SameLine();
        const bool hasSelection = !selection.selectedAssetGuid.empty();
        if (!hasSelection) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Reimport Selected") && hasSelection) {
            const bool ok = assetDatabase.ImportAsset(AssetGuid{ selection.selectedAssetGuid });
            lastOperationMessage_ = ok ? "Selected asset reimported" : "Selected asset reimport failed";
        }
        if (!hasSelection) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("New Folder")) {
            const std::filesystem::path parentDirectory = assetDatabase.GetProjectRoot() / currentDirectory_;
            const std::filesystem::path newFolder = MakeUniqueFolderPath(parentDirectory);
            std::error_code ec{};
            std::filesystem::create_directories(newFolder, ec);
            if (ec) {
                lastOperationMessage_ = "Folder creation failed: " + ec.message();
            } else {
                std::error_code relativeEc{};
                std::filesystem::path relative = std::filesystem::relative(newFolder, assetDatabase.GetProjectRoot(), relativeEc);
                if (!relativeEc) {
                    currentDirectory_ = relative.lexically_normal();
                }
                assetDatabase.ScanAssets(true);
                lastOperationMessage_ = "Folder created";
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Migrate Legacy JSON")) {
            LegacyAssetJsonMigrator migrator{};
            LegacyAssetMigrationOptions options{};
            options.rewriteScenes = true;
            LegacyAssetMigrationResult result = migrator.Migrate(assetDatabase, options);
            assetDatabase.ScanAssets(true);
            lastOperationMessage_ = result.success
                ? "Legacy migration report written"
                : "Legacy migration failed";
        }

        const float filterLineWidth = ImGui::GetContentRegionAvail().x;
        ImGui::SetNextItemWidth((std::max)(220.0f, filterLineWidth * 0.38f));
        ImGui::InputTextWithHint("##AssetSearch", "Search assets...", searchBuffer_.data(), searchBuffer_.size());
        ImGui::SameLine();
        static const char* TypeFilterItems[] = { "All", "Texture", "Model", "Sky", "Material", "VFX" };
        ImGui::TextUnformatted("Type");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::Combo("##AssetTypeFilter", &typeFilter_, TypeFilterItems, IM_ARRAYSIZE(TypeFilterItems));
        ImGui::SameLine();
        static const char* StateFilterItems[] = { "All", "Imported", "Outdated", "Missing", "Error", "Meta Only" };
        ImGui::TextUnformatted("State");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        ImGui::Combo("##AssetStateFilter", &stateFilter_, StateFilterItems, IM_ARRAYSIZE(StateFilterItems));
        ImGui::SameLine();
        static const char* ViewModeItems[] = { "List", "Grid" };
        ImGui::TextUnformatted("View");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(96.0f);
        ImGui::Combo("##AssetViewMode", &viewMode_, ViewModeItems, IM_ARRAYSIZE(ViewModeItems));
        ImGui::SameLine();
        ImGui::Checkbox("Recursive", &recursive_);
        ImGui::PopStyleVar();

        if (!lastOperationMessage_.empty()) {
            ImGui::TextDisabled("%s", lastOperationMessage_.c_str());
        }

        ImGui::Separator();

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float treeWidth = (std::min)(280.0f, (std::max)(180.0f, available.x * 0.24f));
        ImGui::BeginChild("##AssetFolderTree", ImVec2(treeWidth, 0.0f), true);
        ImGui::TextUnformatted("Folders");
        ImGui::Separator();
        for (const std::filesystem::path& directory : assetDatabase.CollectDirectories()) {
            const bool selected = directory.lexically_normal().generic_string() == currentDirectory_.lexically_normal().generic_string();
            if (ImGui::Selectable(directory.generic_string().c_str(), selected)) {
                currentDirectory_ = directory;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##AssetList", ImVec2(0.0f, 0.0f), true);
        ImGui::Text("%s", currentDirectory_.generic_string().c_str());
        ImGui::Separator();

        std::vector<const AssetRecord*> records = assetDatabase.CollectInDirectory(currentDirectory_, recursive_);
        records.erase(std::remove_if(records.begin(), records.end(), [&](const AssetRecord* record) {
            return !record ||
                !MatchesTypeFilter(*record, typeFilter_) ||
                !MatchesStateFilter(*record, stateFilter_) ||
                !MatchesSearch(*record, searchBuffer_.data());
        }), records.end());

        std::sort(records.begin(), records.end(), [](const AssetRecord* lhs, const AssetRecord* rhs) {
            if (!lhs || !rhs) {
                return lhs < rhs;
            }
            return ToLowerCopy(lhs->displayName) < ToLowerCopy(rhs->displayName);
        });

        ImGui::TextDisabled("%d assets shown", static_cast<int>(records.size()));
        ImGui::Separator();

        if (records.empty()) {
            ImGui::Dummy(ImVec2(0.0f, 16.0f));
            ImGui::TextDisabled("No assets here");
            ImGui::TextDisabled("Create folders here, or add source files under Assets and press Refresh.");
        } else if (viewMode_ == 0) {
            DrawRecordList(assetDatabase, records, selection, lastOperationMessage_);
        } else {
            DrawRecordGrid(assetDatabase, records, selection, lastOperationMessage_);
        }

        ImGui::EndChild();
#else
        (void)assetDatabase;
        (void)selection;
#endif
    }

} // namespace HIKARI
