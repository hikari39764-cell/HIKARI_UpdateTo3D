#include "HIKARI_AssetBrowserPanel.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <Windows.h>
#include <Shellapi.h>
#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Assets/Legacy/HIKARI_LegacyAssetJsonMigrator.h"
#include "HIKARI_EditorSelection.h"
#include "Platform/HIKARI_Win32Window.h"
#include "Render2D/HIKARI_DxTexture.h"
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
            case AssetType::Scene: return "Scene";
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
            case AssetType::Scene: return "[Scn]";
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

        void SelectRecord(const AssetRecord& record, EditorSelection& selection);

        bool IsAssetsRootPath(const std::filesystem::path& path) {
            return ToLowerCopy(path.lexically_normal().generic_string()) == "assets";
        }

        bool IsSupportedImportSource(const std::filesystem::path& path) {
            const std::string filename = ToLowerCopy(path.filename().string());
            const std::string ext = ToLowerCopy(path.extension().string());
            return ext == ".png" ||
                ext == ".jpg" ||
                ext == ".jpeg" ||
                ext == ".tga" ||
                ext == ".bmp" ||
                ext == ".dds" ||
                ext == ".hdr" ||
                ext == ".gltf" ||
                ext == ".glb" ||
                ext == ".fbx" ||
                ext == ".obj" ||
                ext == ".hscene" ||
                filename.ends_with(".scene.json") ||
                ext == ".hmat" ||
                filename.ends_with(".mat.json") ||
                ext == ".efk" ||
                ext == ".efkefc";
        }

        bool IsCopyOnlySidecarFile(const std::filesystem::path& path) {
            const std::string ext = ToLowerCopy(path.extension().string());
            return ext == ".bin" ||
                ext == ".mtl";
        }

        std::filesystem::path SuggestedTargetDirectory(
            const std::filesystem::path& currentDirectory,
            const std::filesystem::path& sourcePath) {

            if (!currentDirectory.empty() && !IsAssetsRootPath(currentDirectory)) {
                return currentDirectory;
            }

            const std::string filename = ToLowerCopy(sourcePath.filename().string());
            const std::string ext = ToLowerCopy(sourcePath.extension().string());
            if (ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".obj") {
                return "Assets/Models";
            }
            if (ext == ".hscene" || filename.ends_with(".scene.json")) {
                return "Assets/Scenes";
            }
            if (ext == ".hmat" || filename.ends_with(".mat.json")) {
                return "Assets/Materials";
            }
            if (ext == ".efk" || ext == ".efkefc") {
                return "Assets/Vfx";
            }
            if (ext == ".dds" && (filename.find("sky") != std::string::npos ||
                filename.find("cube") != std::string::npos ||
                filename.find("cubemap") != std::string::npos)) {
                return "Assets/Skies";
            }
            return "Assets/Textures";
        }

        bool IsPathInside(const std::filesystem::path& path, const std::filesystem::path& directory) {
            std::error_code ec{};
            const std::filesystem::path relative = std::filesystem::relative(path, directory, ec);
            if (ec || relative.empty()) {
                return false;
            }
            const std::string native = relative.generic_string();
            return native != "." && native.find("..") != 0;
        }

        std::filesystem::path MakeUniqueFilePath(const std::filesystem::path& absolutePath) {
            std::error_code ec{};
            if (!std::filesystem::exists(absolutePath, ec)) {
                return absolutePath;
            }

            const std::filesystem::path parent = absolutePath.parent_path();
            const std::string stem = absolutePath.stem().string();
            const std::string extension = absolutePath.extension().string();
            for (int i = 1; i < 10000; ++i) {
                std::filesystem::path candidate = parent / (stem + "_" + std::to_string(i) + extension);
                ec.clear();
                if (!std::filesystem::exists(candidate, ec)) {
                    return candidate;
                }
            }
            return parent / (stem + "_9999" + extension);
        }

        bool IsScenesDirectoryPath(const std::filesystem::path& path) {
            const std::string generic = ToLowerCopy(path.lexically_normal().generic_string());
            return generic == "assets/scenes" || generic.rfind("assets/scenes/", 0) == 0;
        }

        bool CreateEmptySceneAsset(
            AssetDatabase& assetDatabase,
            const std::filesystem::path& currentDirectory,
            std::filesystem::path& outRelativePath,
            std::string& outError) {

            const std::filesystem::path sceneDirectory = IsScenesDirectoryPath(currentDirectory)
                ? currentDirectory
                : std::filesystem::path("Assets/Scenes");
            const std::filesystem::path absoluteScenePath = MakeUniqueFilePath(
                (assetDatabase.GetProjectRoot() / sceneDirectory / "New Scene.scene.json").lexically_normal());

            std::error_code ec{};
            std::filesystem::create_directories(absoluteScenePath.parent_path(), ec);
            if (ec) {
                outError = "Failed to create scene directory: " + ec.message();
                return false;
            }

            const std::string sceneName = absoluteScenePath.stem().stem().string();
            const nlohmann::json sceneJson{
                { "version", 1 },
                { "sceneName", sceneName.empty() ? "New Scene" : sceneName },
                { "objects", nlohmann::json::array() },
                { "environment", {
                    { "ambient", {
                        { "color", nlohmann::json::array({ 1.0f, 1.0f, 1.0f }) },
                        { "intensity", 0.2f },
                    } },
                    { "directional", {
                        { "enabled", true },
                        { "color", nlohmann::json::array({ 1.0f, 1.0f, 1.0f }) },
                        { "direction", nlohmann::json::array({ 0.26832816f, -0.89442718f, -0.35777089f }) },
                        { "intensity", 1.0f },
                    } },
                    { "pointLights", nlohmann::json::array() },
                    { "sky", {
                        { "enabled", false },
                        { "skyAsset", "" },
                        { "exposure", 1.0f },
                        { "followCamera", true },
                        { "scale", 0.05f },
                        { "tint", nlohmann::json::array({ 1.0f, 1.0f, 1.0f }) },
                        { "yaw", 0.0f },
                    } },
                    { "specularIntensity", 0.2f },
                    { "specularPower", 32.0f },
                } },
            };

            std::ofstream ofs(absoluteScenePath);
            if (!ofs.is_open()) {
                outError = "Failed to write scene file: " + absoluteScenePath.generic_string();
                return false;
            }
            ofs << sceneJson.dump(2) << '\n';

            outRelativePath = std::filesystem::relative(absoluteScenePath, assetDatabase.GetProjectRoot(), ec).lexically_normal();
            if (ec) {
                outRelativePath = absoluteScenePath.lexically_normal();
            }
            return true;
        }

        bool CopySourceFileIntoProject(
            const AssetDatabase& assetDatabase,
            const std::filesystem::path& sourceFile,
            const std::filesystem::path& targetRelativePath,
            std::filesystem::path& outRelativePath,
            std::string& outError) {

            std::error_code ec{};
            const std::filesystem::path sourceAbsolute = std::filesystem::absolute(sourceFile, ec).lexically_normal();
            if (ec) {
                outError = "Failed to resolve source path: " + ec.message();
                return false;
            }

            if (IsPathInside(sourceAbsolute, assetDatabase.GetAssetsRoot())) {
                outRelativePath = std::filesystem::relative(sourceAbsolute, assetDatabase.GetProjectRoot(), ec).lexically_normal();
                if (ec) {
                    outError = "Failed to make source path project-relative: " + ec.message();
                    return false;
                }
                return true;
            }

            std::filesystem::path destinationAbsolute = assetDatabase.GetProjectRoot() / targetRelativePath;
            destinationAbsolute = MakeUniqueFilePath(destinationAbsolute.lexically_normal());
            std::filesystem::create_directories(destinationAbsolute.parent_path(), ec);
            if (ec) {
                outError = "Failed to create target directory: " + ec.message();
                return false;
            }

            std::filesystem::copy_file(sourceAbsolute, destinationAbsolute, std::filesystem::copy_options::none, ec);
            if (ec) {
                outError = "Failed to copy " + sourceAbsolute.generic_string() + ": " + ec.message();
                return false;
            }

            outRelativePath = std::filesystem::relative(destinationAbsolute, assetDatabase.GetProjectRoot(), ec).lexically_normal();
            if (ec) {
                outError = "Failed to make imported path project-relative: " + ec.message();
                return false;
            }
            return true;
        }

        void CollectDroppedFiles(
            const std::filesystem::path& droppedPath,
            const std::filesystem::path& currentDirectory,
            const AssetDatabase& assetDatabase,
            std::vector<std::filesystem::path>& outProjectRelativeFiles,
            int& skippedCount,
            std::string& lastError) {

            std::error_code ec{};
            if (std::filesystem::is_directory(droppedPath, ec)) {
                const std::filesystem::path targetRoot = (currentDirectory.empty() || IsAssetsRootPath(currentDirectory))
                    ? std::filesystem::path("Assets") / droppedPath.filename()
                    : currentDirectory / droppedPath.filename();

                for (const auto& entry : std::filesystem::recursive_directory_iterator(droppedPath, ec)) {
                    if (ec) {
                        lastError = "Failed to read dropped folder: " + ec.message();
                        break;
                    }
                    if (!entry.is_regular_file(ec)) {
                        ++skippedCount;
                        continue;
                    }
                    const bool supportedAsset = IsSupportedImportSource(entry.path());
                    const bool copyOnlySidecar = IsCopyOnlySidecarFile(entry.path());
                    if (!supportedAsset && !copyOnlySidecar) {
                        ++skippedCount;
                        continue;
                    }

                    std::filesystem::path relativeInside = std::filesystem::relative(entry.path(), droppedPath, ec);
                    if (ec) {
                        ++skippedCount;
                        continue;
                    }
                    std::filesystem::path copiedRelative{};
                    const std::filesystem::path targetRelative = (targetRoot / relativeInside).lexically_normal();
                    if (CopySourceFileIntoProject(assetDatabase, entry.path(), targetRelative, copiedRelative, lastError)) {
                        if (supportedAsset) {
                            outProjectRelativeFiles.push_back(copiedRelative);
                        }
                    } else {
                        ++skippedCount;
                    }
                }
                return;
            }

            if (!std::filesystem::is_regular_file(droppedPath, ec) || !IsSupportedImportSource(droppedPath)) {
                ++skippedCount;
                return;
            }

            const std::filesystem::path targetDirectory = SuggestedTargetDirectory(currentDirectory, droppedPath);
            const std::filesystem::path targetRelative = (targetDirectory / droppedPath.filename()).lexically_normal();
            std::filesystem::path copiedRelative{};
            if (CopySourceFileIntoProject(assetDatabase, droppedPath, targetRelative, copiedRelative, lastError)) {
                outProjectRelativeFiles.push_back(copiedRelative);
            } else {
                ++skippedCount;
            }
        }

        void ProcessDroppedFiles(
            AssetDatabase& assetDatabase,
            const std::filesystem::path& currentDirectory,
            EditorSelection& selection,
            std::string& lastOperationMessage) {

            std::vector<std::filesystem::path> droppedFiles = PLATFORM::ConsumeDroppedFiles();
            if (droppedFiles.empty()) {
                return;
            }

            std::vector<std::filesystem::path> copiedFiles;
            int skippedCount = 0;
            std::string lastError{};
            for (const std::filesystem::path& dropped : droppedFiles) {
                CollectDroppedFiles(dropped, currentDirectory, assetDatabase, copiedFiles, skippedCount, lastError);
            }

            if (copiedFiles.empty()) {
                lastOperationMessage = lastError.empty()
                    ? "Drop ignored: no supported asset files"
                    : lastError;
                return;
            }

            assetDatabase.ScanAssets(true);

            int imported = 0;
            int failed = 0;
            std::filesystem::path firstRelativePath{};
            for (const std::filesystem::path& relativePath : copiedFiles) {
                const AssetRecord* record = assetDatabase.FindByPath(relativePath);
                if (!record) {
                    ++failed;
                    continue;
                }
                if (firstRelativePath.empty()) {
                    firstRelativePath = relativePath;
                }
                if (assetDatabase.ImportAsset(record->guid)) {
                    ++imported;
                } else {
                    ++failed;
                }
            }

            assetDatabase.ScanAssets(false);
            if (!firstRelativePath.empty()) {
                if (const AssetRecord* refreshed = assetDatabase.FindByPath(firstRelativePath)) {
                    SelectRecord(*refreshed, selection);
                }
            }

            lastOperationMessage =
                "Dropped " + std::to_string(copiedFiles.size()) +
                " file(s), imported " + std::to_string(imported) +
                ", failed " + std::to_string(failed);
            if (skippedCount > 0) {
                lastOperationMessage += ", skipped " + std::to_string(skippedCount);
            }
        }

        AssetType TypeFromFilterIndex(int index) {
            switch (index) {
            case 1: return AssetType::Texture;
            case 2: return AssetType::Model;
            case 3: return AssetType::Scene;
            case 4: return AssetType::Sky;
            case 5: return AssetType::Material;
            case 6: return AssetType::VfxEffect;
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

        bool HasArtifactFormat(const AssetRecord& record, std::string_view format) {
            for (const AssetArtifactDesc& artifact : record.meta.artifacts) {
                if (artifact.format == format && !artifact.path.empty()) {
                    return true;
                }
            }
            return false;
        }

        bool IsBrokenRecord(const AssetRecord& record) {
            const AssetImportState state = GetImportState(record);
            return state == AssetImportState::MissingSource ||
                state == AssetImportState::MissingMeta ||
                state == AssetImportState::MissingArtifact ||
                state == AssetImportState::UnknownImporter ||
                state == AssetImportState::DuplicateGuid ||
                state == AssetImportState::ImportFailed;
        }

        bool MatchesScope(
            const AssetRecord& record,
            const AssetUsageSummary* usageSummary,
            AssetBrowserScope scope) {

            switch (scope) {
            case AssetBrowserScope::CurrentScene:
                return usageSummary && usageSummary->IsUsed(record.guid);
            case AssetBrowserScope::UnusedInScene:
                return usageSummary && !usageSummary->IsUsed(record.guid);
            case AssetBrowserScope::Broken:
                return IsBrokenRecord(record);
            case AssetBrowserScope::Textures:
                return record.type == AssetType::Texture;
            case AssetBrowserScope::Models:
                return record.type == AssetType::Model;
            case AssetBrowserScope::Scenes:
                return record.type == AssetType::Scene;
            case AssetBrowserScope::Materials:
                return record.type == AssetType::Material;
            case AssetBrowserScope::Skies:
                return record.type == AssetType::Sky;
            case AssetBrowserScope::Vfx:
                return record.type == AssetType::VfxEffect;
            case AssetBrowserScope::Project:
            default:
                return true;
            }
        }

        const char* ToScopeTitle(AssetBrowserScope scope) {
            switch (scope) {
            case AssetBrowserScope::CurrentScene: return "Current Scene";
            case AssetBrowserScope::UnusedInScene: return "Unused In Scene";
            case AssetBrowserScope::Broken: return "Broken Assets";
            case AssetBrowserScope::Textures: return "Textures";
            case AssetBrowserScope::Models: return "Models";
            case AssetBrowserScope::Scenes: return "Scenes";
            case AssetBrowserScope::Materials: return "Materials";
            case AssetBrowserScope::Skies: return "Skies";
            case AssetBrowserScope::Vfx: return "VFX";
            case AssetBrowserScope::Project:
            default: return "Project Assets";
            }
        }

        const char* ToUsageBadge(const AssetRecord& record, const AssetUsageSummary* usageSummary) {
            if (!usageSummary) {
                return "";
            }
            return usageSummary->IsUsed(record.guid) ? "Used" : "Unused";
        }

        const char* ToCookedBadge(const AssetRecord& record) {
            if (record.type == AssetType::Texture) {
                if (HasArtifactFormat(record, "HTEX")) {
                    return "HTEX Ready";
                }
                if (HasArtifactFormat(record, "DDS")) {
                    return "DDS Only";
                }
            }
            if (record.type == AssetType::Model) {
                return HasArtifactFormat(record, "HMODEL") ? "HMODEL Ready" : "Raw Model";
            }
            if (record.type == AssetType::Scene) {
                return "Scene JSON";
            }
            return "";
        }

        const char* ToCompactTypeBadge(AssetType type) {
            switch (type) {
            case AssetType::Texture: return "TEX";
            case AssetType::Model: return "MDL";
            case AssetType::Scene: return "SCN";
            case AssetType::Material: return "MAT";
            case AssetType::Sky: return "SKY";
            case AssetType::VfxEffect: return "VFX";
            case AssetType::Animation: return "ANI";
            case AssetType::Particle: return "PTC";
            case AssetType::Unknown:
            default: return "UNK";
            }
        }

        const char* ToCompactStateBadge(AssetImportState state) {
            switch (state) {
            case AssetImportState::Imported: return "OK";
            case AssetImportState::Outdated: return "OUT";
            case AssetImportState::MetaOnly: return "META";
            case AssetImportState::MissingSource: return "MISS";
            case AssetImportState::MissingMeta: return "META?";
            case AssetImportState::MissingArtifact: return "ART?";
            case AssetImportState::UnknownImporter: return "IMP?";
            case AssetImportState::DuplicateGuid: return "DUP";
            case AssetImportState::ImportFailed: return "ERR";
            case AssetImportState::Unknown:
            default: return "UNK";
            }
        }

        const char* ToCompactUsageBadge(const AssetRecord& record, const AssetUsageSummary* usageSummary) {
            if (!usageSummary) {
                return "";
            }
            return usageSummary->IsUsed(record.guid) ? "USE" : "IDLE";
        }

        const char* ToCompactCookedBadge(const AssetRecord& record) {
            if (record.type == AssetType::Texture) {
                if (HasArtifactFormat(record, "HTEX")) {
                    return "HTEX";
                }
                if (HasArtifactFormat(record, "DDS")) {
                    return "DDS";
                }
                return "RAW";
            }
            if (record.type == AssetType::Model) {
                return HasArtifactFormat(record, "HMODEL") ? "HMDL" : "RAW";
            }
            if (record.type == AssetType::Scene) {
                return "JSON";
            }
            return "";
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

#if defined(_DEBUG)
        // 詳細はホバー時だけ表示し、一覧の密度を保つ。
        void DrawRecordTooltip(const AssetRecord& record) {
            if (!ImGui::BeginTooltip()) {
                return;
            }

            const AssetImportState state = GetImportState(record);
            ImGui::Text("%s %s", ToAssetIcon(record.type), record.displayName.c_str());
            ImGui::TextDisabled("%s | %s", ToAssetTypeText(record.type), ToString(state));
            ImGui::Separator();
            ImGui::Text("GUID: %s", record.guid.value.empty() ? "<none>" : record.guid.value.c_str());
            ImGui::Text("Source: %s", record.sourcePath.generic_string().c_str());
            ImGui::Text("Meta: %s", record.metaPath.generic_string().c_str());
            ImGui::Text("Importer: %s v%u",
                record.meta.importerId.empty() ? "<none>" : record.meta.importerId.c_str(),
                record.meta.importerVersion);
            const std::string artifactPath = FirstArtifactPath(record);
            ImGui::Text("Artifact: %s", artifactPath.empty() ? "<none>" : artifactPath.c_str());
            ImGui::Text("Dependencies: %d", static_cast<int>(record.meta.dependencies.size()));
            ImGui::EndTooltip();
        }

        void DrawCompactBadge(const char* label, const ImVec4& color) {
            if (!label || label[0] == '\0') {
                ImGui::TextDisabled("-");
                return;
            }
            ImGui::TextColored(color, "[%s]", label);
        }

        ImVec4 TypeBadgeColor(AssetType type) {
            switch (type) {
            case AssetType::Texture: return ImVec4(0.55f, 0.78f, 1.0f, 1.0f);
            case AssetType::Model: return ImVec4(0.76f, 0.70f, 1.0f, 1.0f);
            case AssetType::Scene: return ImVec4(0.66f, 0.88f, 0.68f, 1.0f);
            case AssetType::Material: return ImVec4(1.0f, 0.78f, 0.48f, 1.0f);
            case AssetType::Sky: return ImVec4(0.54f, 0.90f, 0.92f, 1.0f);
            case AssetType::VfxEffect: return ImVec4(1.0f, 0.62f, 0.74f, 1.0f);
            default: return ImVec4(0.72f, 0.74f, 0.78f, 1.0f);
            }
        }

        ImVec4 UsageBadgeColor(const char* label) {
            if (label && std::string_view(label) == "USE") {
                return ImVec4(0.60f, 0.86f, 0.60f, 1.0f);
            }
            return ImVec4(0.62f, 0.66f, 0.72f, 1.0f);
        }

        ImVec4 CookedBadgeColor(const char* label) {
            if (!label || label[0] == '\0') {
                return ImVec4(0.62f, 0.66f, 0.72f, 1.0f);
            }
            if (std::string_view(label) == "HTEX" || std::string_view(label) == "HMDL") {
                return ImVec4(0.54f, 0.82f, 1.0f, 1.0f);
            }
            if (std::string_view(label) == "JSON") {
                return ImVec4(0.66f, 0.88f, 0.68f, 1.0f);
            }
            if (std::string_view(label) == "DDS") {
                return ImVec4(0.92f, 0.78f, 0.42f, 1.0f);
            }
            return ImVec4(0.62f, 0.66f, 0.72f, 1.0f);
        }

        int IconAtlasIndexForType(AssetType type) {
            switch (type) {
            case AssetType::Texture: return 1;
            case AssetType::Model: return 2;
            case AssetType::Material: return 3;
            case AssetType::Sky: return 4;
            case AssetType::Scene: return 5;
            case AssetType::VfxEffect: return 6;
            default: return 10;
            }
        }

        bool DrawIconAtlasCell(int iconIndex, const ImVec2& size) {
            static int iconAtlasHandle = -2;
            if (iconAtlasHandle == -2) {
                iconAtlasHandle = DXTEX::DxTextureManager::LoadTextureSrgb(
                    "editor/asset_icon_atlas",
                    "HIKARI/Icon/hikari_asset_icons.png");
            }
            if (iconAtlasHandle < 0) {
                return false;
            }

            const D3D12_GPU_DESCRIPTOR_HANDLE srv =
                DXTEX::DxTextureManager::GetSrvGpuHandle(iconAtlasHandle);
            if (srv.ptr == 0) {
                return false;
            }

            constexpr int kColumns = 4;
            constexpr int kRows = 3;
            const int clampedIndex = (std::max)(0, (std::min)(iconIndex, kColumns * kRows - 1));
            const int column = clampedIndex % kColumns;
            const int row = clampedIndex / kColumns;
            const ImVec2 uv0{
                static_cast<float>(column) / static_cast<float>(kColumns),
                static_cast<float>(row) / static_cast<float>(kRows)
            };
            const ImVec2 uv1{
                static_cast<float>(column + 1) / static_cast<float>(kColumns),
                static_cast<float>(row + 1) / static_cast<float>(kRows)
            };
            ImGui::Image(
                reinterpret_cast<ImTextureID>(static_cast<uintptr_t>(srv.ptr)),
                size,
                uv0,
                uv1);
            return true;
        }

        bool DrawAssetTypeIcon(AssetType type, const ImVec2& size = ImVec2(18.0f, 18.0f)) {
            return DrawIconAtlasCell(IconAtlasIndexForType(type), size);
        }
#endif

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

        void SelectRecord(const AssetRecord& record, EditorSelection& selection) {
            selection.selectedAssetGuid = record.guid.value;
            selection.selectedAssetPath = record.sourcePath.generic_string();
            selection.selectedAsset = nullptr;
        }

#if defined(_DEBUG)
        void DrawAssetDragSource(const AssetRecord& record) {
            if (!record.guid.IsValid()) {
                return;
            }
            if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload(
                    "HIKARI_ASSET_GUID",
                    record.guid.value.c_str(),
                    record.guid.value.size() + 1u);
                const std::string displayName = record.displayName.empty()
                    ? record.sourcePath.filename().string()
                    : record.displayName;
                ImGui::Text("%s", displayName.c_str());
                ImGui::TextDisabled("%s", record.sourcePath.generic_string().c_str());
                ImGui::EndDragDropSource();
            }
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
                const AssetImportBatchResult result = assetDatabase.ImportDependencies(record.guid, false);
                lastOperationMessage =
                    "Dependencies imported " + std::to_string(result.succeeded) +
                    ", failed " + std::to_string(result.failed);
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
            const AssetUsageSummary* usageSummary,
            std::string& lastOperationMessage) {

            if (!ImGui::BeginTable(
                "AssetBrowserTable",
                6,
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_BordersInnerV |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_ScrollY)) {
                return;
            }

            ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 80.0f);
            ImGui::TableSetupColumn("Usage", ImGuiTableColumnFlags_WidthFixed, 74.0f);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 130.0f);
            ImGui::TableSetupColumn("Cooked", ImGuiTableColumnFlags_WidthFixed, 92.0f);
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
                const bool drewIcon = DrawAssetTypeIcon(record->type);
                if (drewIcon) {
                    ImGui::SameLine();
                }
                const std::string label =
                    std::string(drewIcon ? "" : ToAssetIcon(record->type)) +
                    (drewIcon ? "" : " ") +
                    record->displayName + "##" +
                    record->sourcePath.generic_string();
                if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                    SelectRecord(*record, selection);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && record->type == AssetType::Model) {
                        lastOperationMessage = "Model selected: " + record->displayName;
                    }
                }
                const bool rowHovered = ImGui::IsItemHovered();
                DrawAssetDragSource(*record);
                if (rowHovered) {
                    DrawRecordTooltip(*record);
                }

                if (ImGui::BeginPopupContextItem()) {
                    DrawRecordContextMenu(assetDatabase, *record, selection, lastOperationMessage);
                    ImGui::EndPopup();
                }

                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(ToAssetTypeText(record->type));
                ImGui::TableSetColumnIndex(2);
                ImGui::TextDisabled("%s", ToUsageBadge(*record, usageSummary));
                ImGui::TableSetColumnIndex(3);
                const AssetImportState state = GetImportState(*record);
                ImGui::TextColored(StateColor(state), "%s", ToString(state));
                ImGui::TableSetColumnIndex(4);
                ImGui::TextDisabled("%s", ToCookedBadge(*record));
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(record->sourcePath.generic_string().c_str());
                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        void DrawRecordCompactRows(
            AssetDatabase& assetDatabase,
            const std::vector<const AssetRecord*>& records,
            EditorSelection& selection,
            const AssetUsageSummary* usageSummary,
            std::string& lastOperationMessage) {

            if (!ImGui::BeginTable(
                "AssetBrowserCompactRows",
                5,
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_SizingStretchProp)) {
                return;
            }

            ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 56.0f);
            ImGui::TableSetupColumn("Usage", ImGuiTableColumnFlags_WidthFixed, 64.0f);
            ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 68.0f);
            ImGui::TableSetupColumn("Cooked", ImGuiTableColumnFlags_WidthFixed, 76.0f);

            for (const AssetRecord* record : records) {
                if (!record) {
                    continue;
                }

                ImGui::PushID(record->guid.IsValid()
                    ? record->guid.value.c_str()
                    : record->sourcePath.generic_string().c_str());

                ImGui::TableNextRow(0, ImGui::GetFrameHeightWithSpacing());
                ImGui::TableSetColumnIndex(0);
                const bool isSelected = selection.selectedAssetGuid == record->guid.value;
                const std::string displayName = record->displayName.empty()
                    ? record->sourcePath.filename().string()
                    : record->displayName;
                const bool drewIcon = DrawAssetTypeIcon(record->type);
                if (drewIcon) {
                    ImGui::SameLine();
                }
                const std::string label =
                    std::string(drewIcon ? "" : ToAssetIcon(record->type)) +
                    (drewIcon ? "" : " ") +
                    displayName + "##compact_" +
                    record->sourcePath.generic_string();
                if (ImGui::Selectable(
                    label.c_str(),
                    isSelected,
                    ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowDoubleClick)) {
                    SelectRecord(*record, selection);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && record->type == AssetType::Model) {
                        lastOperationMessage = "Model selected: " + record->displayName;
                    }
                }
                const bool rowHovered = ImGui::IsItemHovered();
                DrawAssetDragSource(*record);
                if (rowHovered) {
                    DrawRecordTooltip(*record);
                }
                if (ImGui::BeginPopupContextItem()) {
                    DrawRecordContextMenu(assetDatabase, *record, selection, lastOperationMessage);
                    ImGui::EndPopup();
                }

                const AssetImportState state = GetImportState(*record);
                const char* usageBadge = ToCompactUsageBadge(*record, usageSummary);
                const char* cookedBadge = ToCompactCookedBadge(*record);

                ImGui::TableSetColumnIndex(1);
                DrawCompactBadge(ToCompactTypeBadge(record->type), TypeBadgeColor(record->type));
                ImGui::TableSetColumnIndex(2);
                DrawCompactBadge(usageBadge, UsageBadgeColor(usageBadge));
                ImGui::TableSetColumnIndex(3);
                DrawCompactBadge(ToCompactStateBadge(state), StateColor(state));
                ImGui::TableSetColumnIndex(4);
                DrawCompactBadge(cookedBadge, CookedBadgeColor(cookedBadge));

                ImGui::PopID();
            }

            ImGui::EndTable();
        }

        void DrawRecordGrid(
            AssetDatabase& assetDatabase,
            const std::vector<const AssetRecord*>& records,
            EditorSelection& selection,
            const AssetUsageSummary* usageSummary,
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

                const float iconOffset = (cardWidth - 34.0f) * 0.5f;
                if (iconOffset > 0.0f) {
                    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + iconOffset);
                }
                DrawAssetTypeIcon(record->type, ImVec2(34.0f, 34.0f));

                std::string buttonLabel =
                    record->displayName + "\n" +
                    ToString(GetImportState(*record));
                const char* usageBadge = ToUsageBadge(*record, usageSummary);
                const char* cookedBadge = ToCookedBadge(*record);
                if (usageBadge[0] != '\0') {
                    buttonLabel += std::string(" / ") + usageBadge;
                }
                if (cookedBadge[0] != '\0') {
                    buttonLabel += std::string("\n") + cookedBadge;
                }
                if (ImGui::Button(buttonLabel.c_str(), ImVec2(cardWidth, 96.0f))) {
                    SelectRecord(*record, selection);
                }
                const bool cardHovered = ImGui::IsItemHovered();
                DrawAssetDragSource(*record);
                if (cardHovered) {
                    DrawRecordTooltip(*record);
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
        DrawContents(assetDatabase, selection, nullptr, AssetBrowserScope::Project);
    }

    void AssetBrowserPanel::DrawContents(
        AssetDatabase& assetDatabase,
        EditorSelection& selection,
        const AssetUsageSummary* usageSummary,
        AssetBrowserScope scope) const {
#if defined(_DEBUG)
        if (currentDirectory_.empty()) {
            currentDirectory_ = "Assets";
        }
        ProcessDroppedFiles(assetDatabase, currentDirectory_, selection, lastOperationMessage_);

        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
        ImGui::TextUnformatted(ToScopeTitle(scope));
        ImGui::SameLine();
        if (scope == AssetBrowserScope::Project) {
            ImGui::TextDisabled("%s", currentDirectory_.generic_string().c_str());
        } else {
            ImGui::TextDisabled("project-wide");
        }
        ImGui::SameLine();
        ImGui::TextDisabled("Drop files/folders here to import");
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
        if (!hasSelection) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Import Dependencies") && hasSelection) {
            const AssetImportBatchResult result = assetDatabase.ImportDependencies(
                AssetGuid{ selection.selectedAssetGuid },
                false);
            lastOperationMessage_ =
                "Dependencies imported " + std::to_string(result.succeeded) +
                ", failed " + std::to_string(result.failed);
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
        if (ImGui::Button("New Scene")) {
            std::filesystem::path scenePath{};
            std::string error{};
            if (CreateEmptySceneAsset(assetDatabase, currentDirectory_, scenePath, error)) {
                assetDatabase.ScanAssets(true);
                currentDirectory_ = scenePath.parent_path();
                if (const AssetRecord* sceneRecord = assetDatabase.FindByPath(scenePath)) {
                    SelectRecord(*sceneRecord, selection);
                }
                lastOperationMessage_ = "Scene asset created";
            } else {
                lastOperationMessage_ = error.empty() ? "Scene creation failed" : error;
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
        static const char* TypeFilterItems[] = { "All", "Texture", "Model", "Scene", "Sky", "Material", "VFX" };
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
        static const char* ViewModeItems[] = { "Compact", "List", "Grid" };
        ImGui::TextUnformatted("View");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(118.0f);
        ImGui::Combo("##AssetViewMode", &viewMode_, ViewModeItems, IM_ARRAYSIZE(ViewModeItems));
        ImGui::SameLine();
        ImGui::Checkbox("Recursive", &recursive_);
        ImGui::PopStyleVar();

        if (!lastOperationMessage_.empty()) {
            ImGui::TextDisabled("%s", lastOperationMessage_.c_str());
        }

        ImGui::Separator();

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const bool showFolderTree = scope == AssetBrowserScope::Project;
        if (showFolderTree) {
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
        }

        ImGui::BeginChild("##AssetList", ImVec2(0.0f, 0.0f), true);
        ImGui::Text("%s", scope == AssetBrowserScope::Project
            ? currentDirectory_.generic_string().c_str()
            : ToScopeTitle(scope));
        ImGui::Separator();

        std::vector<const AssetRecord*> records = showFolderTree
            ? assetDatabase.CollectInDirectory(currentDirectory_, recursive_)
            : assetDatabase.CollectAll();
        records.erase(std::remove_if(records.begin(), records.end(), [&](const AssetRecord* record) {
            return !record ||
                !MatchesScope(*record, usageSummary, scope) ||
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
        } else if (viewMode_ == 1) {
            DrawRecordList(assetDatabase, records, selection, usageSummary, lastOperationMessage_);
        } else if (viewMode_ == 2) {
            DrawRecordGrid(assetDatabase, records, selection, usageSummary, lastOperationMessage_);
        } else {
            DrawRecordCompactRows(assetDatabase, records, selection, usageSummary, lastOperationMessage_);
        }

        ImGui::EndChild();
#else
        (void)assetDatabase;
        (void)selection;
        (void)usageSummary;
        (void)scope;
#endif
    }

} // namespace HIKARI
