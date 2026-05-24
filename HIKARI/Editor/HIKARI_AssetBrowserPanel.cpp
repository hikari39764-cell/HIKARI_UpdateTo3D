#include "HIKARI_AssetBrowserPanel.h"
#include <algorithm>
#include <cctype>
#include <filesystem>
#include <string>
#include <vector>
#include <Windows.h>
#include <Shellapi.h>
#include "Assets/HIKARI_AssetDatabase.h"
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

        const char* ToImportStateText(const AssetRecord& record) {
            if (!record.sourceExists) return "Missing Source";
            if (!record.metaExists) return "Missing Meta";
            if (record.duplicateGuid) return "Duplicate GUID";
            if (record.importerMissing) return "Unknown Importer";
            if (record.artifactMissing) return "Missing Artifact";
            if (record.importOutdated) return "Outdated";
            if (record.lastImportSucceeded) return "Imported";
            return "Meta Only";
        }

        std::string ToLowerCopy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        std::filesystem::path NormalizePath(const std::filesystem::path& path) {
            return path.lexically_normal();
        }

        bool IsDirectChildOf(const std::filesystem::path& parent, const std::filesystem::path& child) {
            return NormalizePath(child.parent_path()).generic_string() == NormalizePath(parent).generic_string();
        }

        std::vector<std::filesystem::path> CollectDirectories(const std::filesystem::path& assetsRoot) {
            std::vector<std::filesystem::path> directories;
            directories.push_back("Assets");

            std::error_code ec{};
            if (!std::filesystem::exists(assetsRoot, ec)) {
                return directories;
            }

            const std::filesystem::path projectRoot = assetsRoot.parent_path();
            for (const std::filesystem::directory_entry& entry :
                std::filesystem::recursive_directory_iterator(assetsRoot, std::filesystem::directory_options::skip_permission_denied, ec)) {
                if (ec) {
                    break;
                }
                if (!entry.is_directory(ec)) {
                    continue;
                }
                std::filesystem::path relative = std::filesystem::relative(entry.path(), projectRoot, ec);
                if (!ec) {
                    directories.push_back(relative.lexically_normal());
                }
            }

            std::sort(directories.begin(), directories.end(), [](const auto& lhs, const auto& rhs) {
                return ToLowerCopy(lhs.generic_string()) < ToLowerCopy(rhs.generic_string());
            });
            return directories;
        }

        void ShowInExplorer(const std::filesystem::path& path) {
            const std::wstring param = L"/select,\"" + path.wstring() + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", param.c_str(), nullptr, SW_SHOWNORMAL);
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

    void AssetBrowserPanel::Draw(AssetDatabase& assetDatabase, EditorSelection& selection) const {
#if defined(_DEBUG)
        if (!ImGui::Begin("Asset Browser")) {
            ImGui::End();
            return;
        }

        if (currentDirectory_.empty()) {
            currentDirectory_ = "Assets";
        }

        if (ImGui::Button("Refresh")) {
            assetDatabase.ScanAssets(true);
        }
        ImGui::SameLine();
        if (ImGui::Button("Reimport Selected") && !selection.selectedAssetGuid.empty()) {
            assetDatabase.ImportAsset(AssetGuid{ selection.selectedAssetGuid });
        }

        ImGui::Separator();

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float treeWidth = (std::max)(180.0f, available.x * 0.28f);
        ImGui::BeginChild("##AssetFolderTree", ImVec2(treeWidth, 0.0f), true);
        for (const std::filesystem::path& directory : CollectDirectories(assetDatabase.GetAssetsRoot())) {
            const bool selected = NormalizePath(directory).generic_string() == NormalizePath(currentDirectory_).generic_string();
            if (ImGui::Selectable(directory.generic_string().c_str(), selected)) {
                currentDirectory_ = directory;
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();

        ImGui::BeginChild("##AssetList", ImVec2(0.0f, 0.0f), true);
        ImGui::Text("Folder: %s", currentDirectory_.generic_string().c_str());
        ImGui::Separator();

        if (ImGui::BeginTable("AssetBrowserTable", 5, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable)) {
            ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Importer", ImGuiTableColumnFlags_WidthFixed, 150.0f);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 120.0f);
            ImGui::TableSetupColumn("Path", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableHeadersRow();

            const std::vector<const AssetRecord*> records = assetDatabase.CollectAll();
            for (const AssetRecord* record : records) {
                if (!record || !IsDirectChildOf(currentDirectory_, record->sourcePath)) {
                    continue;
                }

                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                const bool isSelected = selection.selectedAssetGuid == record->guid.value;
                const std::string label = std::string(ToAssetIcon(record->type)) + " " + record->displayName + "##" + record->sourcePath.generic_string();
                if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                    selection.selectedAssetGuid = record->guid.value;
                    selection.selectedAssetPath = record->sourcePath.generic_string();
                    selection.selectedAsset = nullptr;
                }

                if (ImGui::BeginPopupContextItem()) {
                    if (ImGui::MenuItem("Reimport")) {
                        assetDatabase.ImportAsset(record->guid);
                    }
                    if (ImGui::MenuItem("Show in Explorer")) {
                        ShowInExplorer(assetDatabase.GetProjectRoot() / record->sourcePath);
                    }
                    if (ImGui::MenuItem("Copy GUID")) {
                        ImGui::SetClipboardText(record->guid.value.c_str());
                    }
                    if (ImGui::MenuItem("Copy Asset Path")) {
                        ImGui::SetClipboardText(record->sourcePath.generic_string().c_str());
                    }
                    ImGui::EndPopup();
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(ToAssetTypeText(record->type));
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(record->meta.importerId.empty() ? "<none>" : record->meta.importerId.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(ToImportStateText(*record));
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(record->sourcePath.generic_string().c_str());
            }

            ImGui::EndTable();
        }

        ImGui::EndChild();
        ImGui::End();
#else
        (void)assetDatabase;
        (void)selection;
#endif
    }

} // namespace HIKARI
