#include "HIKARI_AssetBrowserPanel.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <Windows.h>
#include <Shellapi.h>
#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/HIKARI_AssetUsageAnalyzer.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Core/HIKARI_Logger.h"
#include "Editor/DragDrop/HIKARI_EditorAssetDragDrop.h"
#include "Editor/Style/HIKARI_EditorIconManager.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Platform/HIKARI_Win32Window.h"
#include "Project/HIKARI_ProjectSettings.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

    namespace {
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
            case AssetType::Sequence: return "Sequence";
            case AssetType::Unknown:
            default: return "Unknown";
            }
        }

        const char* ToAssetIcon(AssetType type) {
            return EDITOR::EditorIconManager::GetAssetFallbackText(type);
        }

        std::string ToLowerCopy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        bool EndsWithCaseInsensitive(const std::string& value, std::string_view suffix) {
            if (suffix.size() > value.size()) {
                return false;
            }
            const std::string tail = ToLowerCopy(value.substr(value.size() - suffix.size()));
            return tail == ToLowerCopy(std::string(suffix));
        }

        void SelectRecord(const AssetRecord& record, EditorSelection& selection);
        void LogSceneAssetInfo(const std::string& message);
        void LogSceneAssetWarn(const std::string& message);
        std::filesystem::path MakeUniqueFolderPath(const std::filesystem::path& parentDirectory);

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
                filename.ends_with(".material.json") ||
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
            if (ext == ".hmat" || filename.ends_with(".material.json")) {
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

        std::filesystem::path MakeUniqueCompoundSuffixFilePath(
            const std::filesystem::path& absoluteDirectory,
            const std::string& baseName,
            const std::string& compoundSuffix) {

            std::error_code ec{};
            std::filesystem::path candidate = absoluteDirectory / (baseName + compoundSuffix);
            if (!std::filesystem::exists(candidate, ec)) {
                return candidate;
            }

            for (int i = 1; i < 10000; ++i) {
                candidate = absoluteDirectory /
                    (baseName + "_" + std::to_string(i) + compoundSuffix);
                ec.clear();
                if (!std::filesystem::exists(candidate, ec)) {
                    return candidate;
                }
            }

            return absoluteDirectory / (baseName + "_9999" + compoundSuffix);
        }

        std::string SanitizeFileToken(const std::string& raw, const std::string& fallback) {
            std::string out{};
            out.reserve(raw.size());
            for (char ch : raw) {
                const unsigned char c = static_cast<unsigned char>(ch);
                if (std::isalnum(c) != 0 || ch == '_' || ch == '-') {
                    out.push_back(ch);
                } else if (std::isspace(c) != 0) {
                    out.push_back(' ');
                }
            }

            while (!out.empty() && std::isspace(static_cast<unsigned char>(out.front())) != 0) {
                out.erase(out.begin());
            }
            while (!out.empty() && std::isspace(static_cast<unsigned char>(out.back())) != 0) {
                out.pop_back();
            }
            return out.empty() ? fallback : out;
        }

        std::string GetSceneAssetBaseName(const std::filesystem::path& path) {
            std::string filename = path.filename().string();
            const std::string lower = ToLowerCopy(filename);
            constexpr std::string_view kSceneJsonSuffix = ".scene.json";
            const std::string suffix(kSceneJsonSuffix);
            if (lower.size() >= kSceneJsonSuffix.size() &&
                lower.compare(lower.size() - suffix.size(), suffix.size(), suffix) == 0) {
                filename.resize(filename.size() - kSceneJsonSuffix.size());
                return filename;
            }
            return path.stem().string();
        }

        bool LoadJsonFile(const std::filesystem::path& path, nlohmann::json& outRoot) {
            std::ifstream ifs(path);
            if (!ifs.is_open()) {
                return false;
            }
            outRoot = nlohmann::json::parse(ifs, nullptr, false);
            return !outRoot.is_discarded() && outRoot.is_object();
        }

        bool SaveJsonFile(const std::filesystem::path& path, const nlohmann::json& root) {
            std::ofstream ofs(path);
            if (!ofs.is_open()) {
                return false;
            }
            ofs << root.dump(2) << '\n';
            return true;
        }

        bool IsScenesDirectoryPath(const std::filesystem::path& path) {
            const std::string generic = ToLowerCopy(path.lexically_normal().generic_string());
            return generic == "assets/scenes" || generic.rfind("assets/scenes/", 0) == 0;
        }

        bool IsMaterialsDirectoryPath(const std::filesystem::path& path) {
            const std::string generic = ToLowerCopy(path.lexically_normal().generic_string());
            return generic == "assets/materials" || generic.rfind("assets/materials/", 0) == 0;
        }

        bool CreateDefaultMaterialAsset(
            AssetDatabase& assetDatabase,
            const std::filesystem::path& currentDirectory,
            std::filesystem::path& outRelativePath,
            std::string& outError) {

            const std::filesystem::path materialDirectory = IsMaterialsDirectoryPath(currentDirectory)
                ? currentDirectory
                : std::filesystem::path("Assets/Materials");
            const std::filesystem::path absoluteMaterialDirectory =
                (assetDatabase.GetProjectRoot() / materialDirectory).lexically_normal();
            const std::filesystem::path absoluteMaterialPath =
                MakeUniqueCompoundSuffixFilePath(
                    absoluteMaterialDirectory,
                    "New Material",
                    ".material.json");

            PbrMaterialAssetData data{};
            data.materialName = absoluteMaterialPath.stem().stem().string();
            if (data.materialName.empty()) {
                data.materialName = "New Material";
            }

            // Material Asset 邵ｺ・ｯ Texture 邵ｺ・ｮ GUID 郢ｧ蜑・ｽｿ譎・亜邵ｺ蜉ｱﾂ竏晢ｽｮ貊・怙邵ｺ・ｮ HTEX 邵ｺ・ｯ runtime builder 邵ｺ迹夲ｽｧ・｣雎趣ｽｺ邵ｺ蜷ｶ・狗ｸｲ繝ｻ
            if (!SavePbrMaterialAssetData(absoluteMaterialPath, data, outError)) {
                return false;
            }

            std::error_code relativeEc{};
            outRelativePath = std::filesystem::relative(
                absoluteMaterialPath,
                assetDatabase.GetProjectRoot(),
                relativeEc).lexically_normal();
            if (relativeEc) {
                outError = "Failed to resolve material path: " + relativeEc.message();
                return false;
            }
            return true;
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
                { "systems", nlohmann::json::array({
                    {
                        { "systemId", "TransformSystem" },
                        { "enabled", true },
                        { "executionOrder", 0 },
                        { "settings", nlohmann::json::object() }
                    },
                    {
                        { "systemId", "ModelRenderSystem" },
                        { "enabled", true },
                        { "executionOrder", 100 },
                        { "settings", nlohmann::json::object() }
                    },
                    {
                        { "systemId", "AnimationSystem" },
                        { "enabled", true },
                        { "executionOrder", 150 },
                        { "settings", nlohmann::json::object() }
                    },
                    {
                        { "systemId", "VfxSystem" },
                        { "enabled", true },
                        { "executionOrder", 200 },
                        { "settings", nlohmann::json::object() }
                    },
                    {
                        { "systemId", "ScriptSystem" },
                        { "enabled", false },
                        { "executionOrder", 400 },
                        { "settings", nlohmann::json::object() }
                    },
                }) },
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

        bool CreateFolderFromBrowser(
            AssetDatabase& assetDatabase,
            std::filesystem::path& currentDirectory,
            std::string& lastOperationMessage) {

            const std::filesystem::path parentDirectory = assetDatabase.GetProjectRoot() / currentDirectory;
            const std::filesystem::path newFolder = MakeUniqueFolderPath(parentDirectory);
            std::error_code ec{};
            std::filesystem::create_directories(newFolder, ec);
            if (ec) {
                lastOperationMessage = "Folder creation failed: " + ec.message();
                return false;
            }

            std::error_code relativeEc{};
            std::filesystem::path relative =
                std::filesystem::relative(newFolder, assetDatabase.GetProjectRoot(), relativeEc);
            if (!relativeEc) {
                currentDirectory = relative.lexically_normal();
            }
            assetDatabase.ScanAssets(true);
            lastOperationMessage = "Folder created";
            return true;
        }

        bool CreateSceneFromBrowser(
            AssetDatabase& assetDatabase,
            std::filesystem::path& currentDirectory,
            EditorSelection& selection,
            std::string& lastOperationMessage) {

            std::filesystem::path scenePath{};
            std::string error{};
            if (!CreateEmptySceneAsset(assetDatabase, currentDirectory, scenePath, error)) {
                lastOperationMessage = error.empty() ? "Scene creation failed" : error;
                LogSceneAssetWarn("new scene failed: " + lastOperationMessage);
                return false;
            }

            assetDatabase.ScanAssets(true);
            currentDirectory = scenePath.parent_path();
            if (const AssetRecord* sceneRecord = assetDatabase.FindByPath(scenePath)) {
                SelectRecord(*sceneRecord, selection);
            }
            lastOperationMessage = "Scene asset created";
            LogSceneAssetInfo("new scene: " + scenePath.generic_string());
            return true;
        }

        bool CreateMaterialFromBrowser(
            AssetDatabase& assetDatabase,
            std::filesystem::path& currentDirectory,
            EditorSelection& selection,
            std::string& lastOperationMessage) {

            std::filesystem::path materialPath{};
            std::string error{};
            if (!CreateDefaultMaterialAsset(assetDatabase, currentDirectory, materialPath, error)) {
                lastOperationMessage = error.empty() ? "Material creation failed" : error;
                HIKARI_LOG_WARN("[MaterialAsset] new material failed: " + lastOperationMessage);
                return false;
            }

            assetDatabase.ScanAssets(true);
            currentDirectory = materialPath.parent_path();
            if (const AssetRecord* materialRecord = assetDatabase.FindByPath(materialPath)) {
                SelectRecord(*materialRecord, selection);
            }
            lastOperationMessage = "Material asset created";
            HIKARI_LOG_INFO("[MaterialAsset] new material: " + materialPath.generic_string());
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

                std::filesystem::recursive_directory_iterator it(
                    droppedPath,
                    std::filesystem::directory_options::skip_permission_denied,
                    ec);
                const std::filesystem::recursive_directory_iterator end{};
                if (ec) {
                    lastError = "Failed to read dropped folder: " + ec.message();
                    ++skippedCount;
                    return;
                }

                for (; it != end; it.increment(ec)) {
                    if (ec) {
                        lastError = "Failed to continue reading dropped folder: " + ec.message();
                        ec.clear();
                        ++skippedCount;
                        continue;
                    }

                    const std::filesystem::directory_entry entry = *it;
                    std::error_code entryEc{};
                    if (!entry.is_regular_file(entryEc)) {
                        ++skippedCount;
                        continue;
                    }

                    const std::filesystem::path entryPath = entry.path();
                    const bool supportedAsset = IsSupportedImportSource(entryPath);
                    const bool copyOnlySidecar = IsCopyOnlySidecarFile(entryPath);
                    if (!supportedAsset && !copyOnlySidecar) {
                        ++skippedCount;
                        continue;
                    }

                    std::filesystem::path relativeInside = std::filesystem::relative(entryPath, droppedPath, entryEc);
                    if (entryEc) {
                        ++skippedCount;
                        continue;
                    }
                    std::filesystem::path copiedRelative{};
                    const std::filesystem::path targetRelative = (targetRoot / relativeInside).lexically_normal();
                    if (CopySourceFileIntoProject(assetDatabase, entryPath, targetRelative, copiedRelative, lastError)) {
                        if (supportedAsset) {
                            outProjectRelativeFiles.push_back(copiedRelative);
                        }
                    } else {
                        ++skippedCount;
                    }
                }
                return;
            }

            if (ec) {
                lastError = "Failed to inspect dropped path: " + ec.message();
                ++skippedCount;
                return;
            }

            const bool regularFile = std::filesystem::is_regular_file(droppedPath, ec);
            if (ec) {
                lastError = "Failed to inspect dropped file: " + ec.message();
                ++skippedCount;
                return;
            }

            if (!regularFile || !IsSupportedImportSource(droppedPath)) {
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
                try {
                    CollectDroppedFiles(dropped, currentDirectory, assetDatabase, copiedFiles, skippedCount, lastError);
                } catch (const std::exception& ex) {
                    ++skippedCount;
                    lastError = std::string("Drop failed: ") + ex.what();
                    HIKARI_LOG_ERROR("[AssetBrowser][Drop] " + lastError);
                } catch (...) {
                    ++skippedCount;
                    lastError = "Drop failed: unknown exception";
                    HIKARI_LOG_ERROR("[AssetBrowser][Drop] " + lastError);
                }
            }

            if (copiedFiles.empty()) {
                lastOperationMessage = lastError.empty()
                    ? "Drop ignored: no supported asset files"
                    : lastError;
                return;
            }

            assetDatabase.ScanAssets(true);

            std::vector<AssetGuid> importGuids;
            importGuids.reserve(copiedFiles.size());
            std::filesystem::path firstRelativePath{};
            for (const std::filesystem::path& relativePath : copiedFiles) {
                const AssetRecord* record = assetDatabase.FindByPath(relativePath);
                if (!record) {
                    continue;
                }
                if (firstRelativePath.empty()) {
                    firstRelativePath = relativePath;
                }
                importGuids.push_back(record->guid);
            }

            const AssetImportBatchResult importResult = assetDatabase.ImportAssets(importGuids);

            assetDatabase.ScanAssets(false);
            if (!firstRelativePath.empty()) {
                if (const AssetRecord* refreshed = assetDatabase.FindByPath(firstRelativePath)) {
                    SelectRecord(*refreshed, selection);
                }
            }

            lastOperationMessage =
                "Dropped " + std::to_string(copiedFiles.size()) +
                " file(s), imported " + std::to_string(importResult.succeeded) +
                ", failed " + std::to_string(importResult.failed);
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
            case 7: return AssetType::Sequence;
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
            for (const AssetArtifactDesc& artifact : record.artifactManifest.artifacts) {
                if (artifact.format == format && !artifact.path.empty()) {
                    return true;
                }
            }
            return false;
        }

        nlohmann::json ReadImportSettings(const AssetRecord& record) {
            nlohmann::json settings = nlohmann::json::parse(record.meta.importSettingsJson, nullptr, false);
            return settings.is_object() ? settings : nlohmann::json::object();
        }

        nlohmann::json& EnsureClusterGeometrySettings(nlohmann::json& settings) {
            if (!settings.contains("clusterGeometry") || !settings["clusterGeometry"].is_object()) {
                settings["clusterGeometry"] = nlohmann::json::object();
            }
            return settings["clusterGeometry"];
        }

#if defined(HIKARI_WITH_EDITOR)
        bool DrawModelClusterCookSettings(nlohmann::json& settings) {
            static const char* ModelGeometryProfileItems[] = { "Scene", "Character" };
            nlohmann::json& cluster = EnsureClusterGeometrySettings(settings);
            bool dirty = false;

            bool enabled = cluster.value("enabled", true);
            if (ImGui::Checkbox("Build HCMESH", &enabled)) {
                cluster["enabled"] = enabled;
                dirty = true;
            }

            int profile = 0;
            const std::string profileValue = cluster.value("profile", std::string("Scene"));
            if (profileValue == "Character") {
                profile = 1;
            }
            if (ImGui::Combo("Cook Profile", &profile, ModelGeometryProfileItems, IM_ARRAYSIZE(ModelGeometryProfileItems))) {
                cluster["profile"] = ModelGeometryProfileItems[profile];
                cluster["partitionLargeSurfaces"] = true;
                cluster["largeSurfaceTargetExtent"] = profile == 1 ? 1.25f : 3.0f;
                cluster["partitionMinClusterEstimate"] = profile == 1 ? 4 : 16;
                cluster["compactUnderfilledClusters"] = true;
                cluster["minClusterOccupancyRatio"] = 0.75f;
                cluster["maxNormalBucketClusterOverhead"] = 1.20f;
                cluster["clusterMergeNormalMinDot"] = 0.20f;
                cluster["normalBucketCoherentGroupMinDot"] = 0.35f;
                cluster["normalBucketQualityBonusRatio"] = 0.15f;
                dirty = true;
            }

            const bool characterProfile = profile == 1;
            const float defaultPartitionExtent = characterProfile ? 1.25f : 3.0f;
            int lodCount = cluster.value("maxLodCount", 5);
            if (ImGui::InputInt("LOD Count", &lodCount)) {
                cluster["maxLodCount"] = (std::max)(1, (std::min)(lodCount, 5));
                dirty = true;
            }

            float qualityBias = cluster.value("lodQualityBias", 1.0f);
            if (ImGui::InputFloat("LOD Quality Bias", &qualityBias)) {
                cluster["lodQualityBias"] = (std::max)(0.50f, (std::min)(qualityBias, 4.0f));
                dirty = true;
            }

            bool partition = cluster.value("partitionLargeSurfaces", true);
            if (ImGui::Checkbox("Partition Large Surfaces", &partition)) {
                cluster["partitionLargeSurfaces"] = partition;
                dirty = true;
            }

            float extent = cluster.value("largeSurfaceTargetExtent", defaultPartitionExtent);
            if (ImGui::InputFloat("Partition Target Extent", &extent)) {
                cluster["largeSurfaceTargetExtent"] =
                    (std::max)(characterProfile ? 1.0f : 2.0f, (std::min)(extent, 64.0f));
                dirty = true;
            }

            bool lockBorders = cluster.value("lockPartitionBorders", true);
            if (ImGui::Checkbox("Lock Partition Borders", &lockBorders)) {
                cluster["lockPartitionBorders"] = lockBorders;
                dirty = true;
            }

            if (dirty) {
                settings["meshFormat"] = "HCMESH";
            }
            return dirty;
        }
#endif

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
            case AssetBrowserScope::Sequences:
                return record.type == AssetType::Sequence;
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
            case AssetBrowserScope::Sequences: return "Sequences";
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
                const bool hasHmodel = HasArtifactFormat(record, "HMODEL");
                const bool hasHcmesh = HasArtifactFormat(record, "HCMESH");
                if (hasHmodel && hasHcmesh) {
                    return "HMODEL + HCMESH";
                }
                if (hasHmodel) {
                    return "HMODEL";
                }
                if (hasHcmesh) {
                    return "HCMESH";
                }
                return "Raw Model";
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
            case AssetType::Sequence: return "SEQ";
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
                const bool hasHmodel = HasArtifactFormat(record, "HMODEL");
                const bool hasHcmesh = HasArtifactFormat(record, "HCMESH");
                if (hasHmodel && hasHcmesh) {
                    return "H+HC";
                }
                if (hasHmodel) {
                    return "HMDL";
                }
                if (hasHcmesh) {
                    return "HC";
                }
                return "RAW";
            }
            if (record.type == AssetType::Scene) {
                return "JSON";
            }
            return "";
        }

#if defined(HIKARI_WITH_EDITOR)
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

        std::string BuildGridCardLabel(std::string_view text, float maxWidth) {
            if (text.empty() || maxWidth <= 0.0f) {
                return {};
            }
            if (ImGui::CalcTextSize(text.data(), text.data() + text.size()).x <= maxWidth) {
                return std::string(text);
            }

            // 郢ｧ・ｰ郢晢ｽｪ郢昴・繝ｩ邵ｺ・ｧ邵ｺ・ｯ陷ｷ讎顔√邵ｺ・ｰ邵ｺ莉｣・帝￥・ｭ邵ｺ蜑ｰ・｡・ｨ驕会ｽｺ邵ｺ蜉ｱﾂ竏ｬ・ｩ・ｳ驍擾ｽｰ邵ｺ・ｯ郢昴・繝ｻ郢晢ｽｫ郢昶・繝｣郢晄懊・邵ｺ・ｫ闔会ｽｻ邵ｺ蟶呻ｽ狗ｸｲ繝ｻ
            constexpr const char* kSuffix = "...";
            std::vector<size_t> utf8Ends{};
            for (size_t i = 0; i < text.size();) {
                const unsigned char c = static_cast<unsigned char>(text[i]);
                size_t step = 1;
                if ((c & 0xE0) == 0xC0) {
                    step = 2;
                } else if ((c & 0xF0) == 0xE0) {
                    step = 3;
                } else if ((c & 0xF8) == 0xF0) {
                    step = 4;
                }

                if (i + step > text.size()) {
                    break;
                }
                i += step;
                utf8Ends.push_back(i);
            }

            for (size_t count = utf8Ends.size(); count > 0; --count) {
                std::string candidate{ text.substr(0, utf8Ends[count - 1]) };
                candidate += kSuffix;
                if (ImGui::CalcTextSize(candidate.c_str()).x <= maxWidth) {
                    return candidate;
                }
            }

            return ImGui::CalcTextSize(kSuffix).x <= maxWidth ? std::string(kSuffix) : std::string{};
        }
#endif

        void ShowInExplorer(const std::filesystem::path& path) {
            const std::wstring param = L"/select,\"" + path.wstring() + L"\"";
            ShellExecuteW(nullptr, L"open", L"explorer.exe", param.c_str(), nullptr, SW_SHOWNORMAL);
        }

        std::string FirstArtifactPath(const AssetRecord& record) {
            for (const AssetArtifactDesc& artifact : record.artifactManifest.artifacts) {
                if (!artifact.path.empty()) {
                    return artifact.path;
                }
            }
            return {};
        }

        std::filesystem::path MakeProjectRelativePath(
            const AssetDatabase& assetDatabase,
            const std::filesystem::path& absolutePath) {

            std::error_code ec{};
            std::filesystem::path relative = std::filesystem::relative(
                absolutePath.lexically_normal(),
                assetDatabase.GetProjectRoot(),
                ec);
            return ec ? absolutePath.lexically_normal() : relative.lexically_normal();
        }

        bool IsSameFilePath(
            const std::filesystem::path& lhs,
            const std::filesystem::path& rhs) {

            const std::filesystem::path normalizedLhs = lhs.lexically_normal();
            const std::filesystem::path normalizedRhs = rhs.lexically_normal();
            if (ToLowerCopy(normalizedLhs.generic_string()) == ToLowerCopy(normalizedRhs.generic_string())) {
                return true;
            }

            std::error_code ec{};
            return std::filesystem::equivalent(normalizedLhs, normalizedRhs, ec) && !ec;
        }

        void LogSceneAssetInfo(const std::string& message) {
            HIKARI_LOG_INFO("[SceneAsset] " + message);
        }

        void LogSceneAssetWarn(const std::string& message) {
            HIKARI_LOG_WARN("[SceneAsset] " + message);
        }

        bool MoveFileSafe(
            const std::filesystem::path& from,
            const std::filesystem::path& to,
            std::string& outError) {

            std::error_code ec{};
            if (!std::filesystem::exists(from, ec)) {
                outError = "Source file is missing: " + from.generic_string();
                return false;
            }

            std::filesystem::create_directories(to.parent_path(), ec);
            if (ec) {
                outError = "Failed to create target directory: " + ec.message();
                return false;
            }

            ec.clear();
            if (std::filesystem::exists(to, ec)) {
                outError = "Target file already exists: " + to.generic_string();
                return false;
            }

            ec.clear();
            std::filesystem::rename(from, to, ec);
            if (ec) {
                outError = "Move failed: " + ec.message();
                return false;
            }
            return true;
        }

        void MoveFileBackBestEffort(
            const std::filesystem::path& from,
            const std::filesystem::path& to) {

            std::error_code ec{};
            if (std::filesystem::exists(from, ec) && !std::filesystem::exists(to, ec)) {
                std::filesystem::rename(from, to, ec);
            }
        }

        bool UpdateSceneJsonSceneName(
            const std::filesystem::path& scenePath,
            std::string_view sceneName,
            std::string& outError) {

            nlohmann::json root{};
            if (!LoadJsonFile(scenePath, root)) {
                outError = "Scene JSON could not be read: " + scenePath.generic_string();
                return false;
            }

            root["sceneName"] = std::string(sceneName);
            if (!SaveJsonFile(scenePath, root)) {
                outError = "Scene JSON could not be written: " + scenePath.generic_string();
                return false;
            }
            return true;
        }

        bool UpdateSceneMetaAfterMove(
            AssetDatabase& assetDatabase,
            const AssetRecord& oldRecord,
            const std::filesystem::path& relativeSource,
            const std::filesystem::path& metaPath,
            std::string_view displayName,
            std::string& outError) {

            if (metaPath.empty()) {
                return true;
            }

            std::error_code ec{};
            if (!std::filesystem::exists(metaPath, ec)) {
                return true;
            }

            AssetMeta meta{};
            if (!assetDatabase.ReadMeta(metaPath, meta)) {
                outError = "Scene meta could not be read: " + metaPath.generic_string();
                return false;
            }

            AssetRecord writable = oldRecord;
            writable.sourcePath = relativeSource.lexically_normal();
            writable.metaPath = metaPath;
            writable.displayName = std::string(displayName);
            writable.meta = std::move(meta);
            writable.meta.sourcePath = writable.sourcePath.generic_string();
            writable.meta.displayName = std::string(displayName);

            if (!assetDatabase.WriteMeta(writable)) {
                outError = "Scene meta could not be written: " + metaPath.generic_string();
                return false;
            }
            return true;
        }

        bool DuplicateSceneAsset(
            AssetDatabase& assetDatabase,
            const AssetRecord& record,
            std::filesystem::path& outRelativePath,
            std::string& outError) {

            const std::filesystem::path sourcePath = (assetDatabase.GetProjectRoot() / record.sourcePath).lexically_normal();
            const std::string baseName = GetSceneAssetBaseName(sourcePath);
            const std::filesystem::path targetPath = MakeUniqueFilePath(
                sourcePath.parent_path() / (baseName + " Copy.scene.json"));

            nlohmann::json root{};
            if (!LoadJsonFile(sourcePath, root)) {
                outError = "Scene duplicate failed: source JSON could not be read";
                return false;
            }

            root["sceneName"] = GetSceneAssetBaseName(targetPath);
            if (!SaveJsonFile(targetPath, root)) {
                outError = "Scene duplicate failed: destination could not be written";
                LogSceneAssetWarn("duplicate failed: " + outError);
                return false;
            }

            std::error_code ec{};
            outRelativePath = std::filesystem::relative(targetPath, assetDatabase.GetProjectRoot(), ec).lexically_normal();
            if (ec) {
                outRelativePath = targetPath.lexically_normal();
            }
            LogSceneAssetInfo("duplicated: " + sourcePath.generic_string() + " -> " + targetPath.generic_string());
            return true;
        }

        bool RenameSceneAsset(
            AssetDatabase& assetDatabase,
            const AssetRecord& record,
            std::string_view newName,
            std::filesystem::path& outRelativePath,
            std::string& outError) {

            const std::string cleanName = SanitizeFileToken(std::string(newName), "Scene");
            const std::filesystem::path oldSource = (assetDatabase.GetProjectRoot() / record.sourcePath).lexically_normal();
            const std::filesystem::path desiredSource = (oldSource.parent_path() / (cleanName + ".scene.json")).lexically_normal();

            // 陷ｷ謔滄倹郢晢ｽｪ郢晞亂繝ｻ郢晢｣ｰ邵ｺ・ｧ邵ｺ・ｯ郢晁ｼ斐＜郢ｧ・､郢晢ｽｫ郢ｧ雋櫁劒邵ｺ荵晢ｼ・ｸｺ螢ｹﾂ竏ｬ・｡・ｨ驕会ｽｺ陷ｷ髦ｪ笆｡邵ｺ螟ｧ驟碑ｭ帶ｺ倪・郢ｧ荵敖繝ｻ
            if (IsSameFilePath(oldSource, desiredSource)) {
                if (!UpdateSceneJsonSceneName(oldSource, cleanName, outError)) {
                    LogSceneAssetWarn("rename failed: " + outError);
                    return false;
                }
                if (!UpdateSceneMetaAfterMove(assetDatabase, record, record.sourcePath, record.metaPath, cleanName, outError)) {
                    LogSceneAssetWarn("rename failed: " + outError);
                    return false;
                }
                outRelativePath = record.sourcePath;
                LogSceneAssetInfo("renamed metadata only: " + oldSource.generic_string());
                return true;
            }

            const std::filesystem::path newSource = MakeUniqueFilePath(desiredSource);
            const std::filesystem::path newRelativeSource = MakeProjectRelativePath(assetDatabase, newSource);
            const std::filesystem::path oldMetaPath = record.metaPath;
            const std::filesystem::path newMetaPath = assetDatabase.GetMetaPathForSource(newRelativeSource);

            std::error_code ec{};
            if (!std::filesystem::exists(oldSource, ec)) {
                outError = "Scene rename failed: source is missing";
                LogSceneAssetWarn("rename failed: " + outError);
                return false;
            }

            bool movedScene = false;
            bool movedMeta = false;
            // 驕假ｽｻ陷肴坩ﾂ豈費ｽｸ・ｭ邵ｺ・ｧ陞滂ｽｱ隰ｨ蜉ｱ・邵ｺ貅ｷ・ｰ・ｴ陷ｷ蛹ｻ繝ｻ邵ｲ竏晏ｺ・妙・ｽ邵ｺ・ｪ驕ｽ繝ｻ蟲・ｸｺ・ｧ陷医・繝ｻ鬩溷調・ｽ・ｮ邵ｺ・ｸ隰鯉ｽｻ邵ｺ蜷ｶﾂ繝ｻ
            if (!MoveFileSafe(oldSource, newSource, outError)) {
                outError = "Scene rename failed: " + outError;
                LogSceneAssetWarn("rename failed: " + outError);
                return false;
            }
            movedScene = true;

            if (!oldMetaPath.empty() && std::filesystem::exists(oldMetaPath, ec)) {
                if (!MoveFileSafe(oldMetaPath, newMetaPath, outError)) {
                    if (movedScene) {
                        MoveFileBackBestEffort(newSource, oldSource);
                    }
                    outError = "Scene meta rename failed: " + outError;
                    LogSceneAssetWarn("rename failed: " + outError);
                    return false;
                }
                movedMeta = true;
            }

            if (!UpdateSceneJsonSceneName(newSource, cleanName, outError)) {
                if (movedMeta) {
                    MoveFileBackBestEffort(newMetaPath, oldMetaPath);
                }
                if (movedScene) {
                    MoveFileBackBestEffort(newSource, oldSource);
                }
                LogSceneAssetWarn("rename failed: " + outError);
                return false;
            }

            outRelativePath = newRelativeSource;
            if (!UpdateSceneMetaAfterMove(assetDatabase, record, outRelativePath, newMetaPath, cleanName, outError)) {
                if (movedMeta) {
                    MoveFileBackBestEffort(newMetaPath, oldMetaPath);
                }
                if (movedScene) {
                    MoveFileBackBestEffort(newSource, oldSource);
                }
                LogSceneAssetWarn("rename failed: " + outError);
                return false;
            }

            LogSceneAssetInfo("renamed: " + oldSource.generic_string() + " -> " + newSource.generic_string());
            return true;
        }

        std::filesystem::path MakeSceneTrashDirectory(const AssetDatabase& assetDatabase) {
            const auto ticks = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count();
            return assetDatabase.GetLibraryRoot() / "Trash" / ("scene_" + std::to_string(ticks));
        }

        bool DeleteSceneAssetToTrash(
            AssetDatabase& assetDatabase,
            const AssetRecord& record,
            bool& outClearedStartupScene,
            std::string& outError) {

            outClearedStartupScene = false;
            const std::filesystem::path sourcePath = (assetDatabase.GetProjectRoot() / record.sourcePath).lexically_normal();
            const std::filesystem::path trashDirectory = MakeSceneTrashDirectory(assetDatabase);
            const std::filesystem::path trashSourcePath = trashDirectory / sourcePath.filename();
            const std::filesystem::path metaPath = record.metaPath;
            const std::filesystem::path trashMetaPath = metaPath.empty()
                ? std::filesystem::path{}
                : trashDirectory / "AssetMeta" / record.sourcePath.parent_path() / metaPath.filename();

            std::error_code ec{};
            if (!std::filesystem::exists(sourcePath, ec)) {
                outError = "Scene delete failed: source is missing";
                LogSceneAssetWarn("delete failed: " + outError);
                return false;
            }

            std::filesystem::create_directories(trashDirectory, ec);
            if (ec) {
                outError = "Scene delete failed: " + ec.message();
                LogSceneAssetWarn("delete failed: " + outError);
                return false;
            }

            if (std::filesystem::exists(trashSourcePath, ec)) {
                outError = "Scene delete failed: trash target already exists";
                LogSceneAssetWarn("delete failed: " + outError);
                return false;
            }
            if (!metaPath.empty() && std::filesystem::exists(metaPath, ec)) {
                ec.clear();
                if (std::filesystem::exists(trashMetaPath, ec)) {
                    outError = "Scene delete failed: trash meta target already exists";
                    LogSceneAssetWarn("delete failed: " + outError);
                    return false;
                }
            }

            bool movedScene = false;
            if (!MoveFileSafe(sourcePath, trashSourcePath, outError)) {
                outError = "Scene delete failed: " + outError;
                LogSceneAssetWarn("delete failed: " + outError);
                return false;
            }
            movedScene = true;

            if (!metaPath.empty() && std::filesystem::exists(metaPath, ec)) {
                if (!MoveFileSafe(metaPath, trashMetaPath, outError)) {
                    if (movedScene) {
                        MoveFileBackBestEffort(trashSourcePath, sourcePath);
                    }
                    outError = "Scene meta trash move failed: " + outError;
                    LogSceneAssetWarn("delete failed: " + outError);
                    return false;
                }
            }

            // Startup Scene 郢ｧ雋樒ｎ鬮ｯ・､邵ｺ蜉ｱ笳・撻・ｴ陷ｷ蛹ｻ繝ｻ邵ｲ・｣rojectSettings 邵ｺ・ｮ陷ｿ繧峨・郢ｧ繧・・隴弱ｅ竊楢棔謔ｶ笘・ｸｲ繝ｻ
            ProjectSettingsService settings{};
            settings.Load(assetDatabase.GetProjectRoot());
            if (settings.GetSettings().startupSceneGuid == record.guid) {
                if (settings.SetStartupSceneGuid(AssetGuid{}) && settings.Save()) {
                    outClearedStartupScene = true;
                } else {
                    LogSceneAssetWarn("delete warning: startupSceneGuid could not be cleared");
                }
            }

            LogSceneAssetInfo("deleted to trash: " + sourcePath.generic_string() + " -> " + trashDirectory.generic_string());
            return true;
        }

#if defined(HIKARI_WITH_EDITOR)
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
            ImGui::Text("Dependencies: %d", static_cast<int>(record.artifactManifest.dependencies.size()));
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
            case AssetType::Sequence: return ImVec4(0.72f, 0.68f, 1.0f, 1.0f);
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

        bool DrawAssetTypeIcon(AssetType type, const ImVec2& size = ImVec2(38.0f, 38.0f)) {
            return EDITOR::EditorIconManager::DrawAssetIcon(type, size);
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
            selection.selectedObject = nullptr;
            selection.selectedAssetGuid = record.guid.value;
            selection.selectedAssetPath = record.sourcePath.generic_string();
            selection.selectedAsset = nullptr;
        }

#if defined(HIKARI_WITH_EDITOR)
        void HandleRecordActivated(
            const AssetRecord& record,
            std::string& lastOperationMessage,
            std::string& activatedSceneGuid,
            std::string& activatedSequenceGuid) {
            if (record.type == AssetType::Scene) {
                activatedSceneGuid = record.guid.value;
                lastOperationMessage = "Scene open requested: " + record.displayName;
                LogSceneAssetInfo("open requested: " + record.sourcePath.generic_string());
                return;
            }
            if (record.type == AssetType::Sequence) {
                activatedSequenceGuid = record.guid.value;
                lastOperationMessage =
                    "Sequence open requested: " + record.displayName;
                return;
            }
            if (record.type == AssetType::Model) {
                lastOperationMessage = "Model selected: " + record.displayName;
                return;
            }
            lastOperationMessage = "Asset selected: " + record.displayName;
        }

        std::string SceneBadges(const AssetRecord& record, const AssetBrowserContext* context) {
            if (!context || record.type != AssetType::Scene || !record.guid.IsValid()) {
                return {};
            }

            std::string badges{};
            if (context->currentSceneGuid == record.guid) {
                badges += "  [Current";
                if (context->currentSceneDirty) {
                    badges += "*";
                }
                badges += "]";
            }
            if (context->startupSceneGuid == record.guid) {
                badges += "  [Startup]";
            }
            return badges;
        }

        std::string DisplayNameWithSceneBadges(const AssetRecord& record, const AssetBrowserContext* context) {
            const std::string displayName = record.displayName.empty()
                ? record.sourcePath.filename().string()
                : record.displayName;
            return displayName + SceneBadges(record, context);
        }

        std::string SceneDisplayNameByGuid(const AssetDatabase& assetDatabase, const AssetGuid& guid) {
            if (!guid.IsValid()) {
                return "<none>";
            }
            const AssetRecord* record = assetDatabase.FindByGuid(guid);
            if (!record) {
                return "<missing>";
            }
            return record->displayName.empty() ? record->sourcePath.filename().string() : record->displayName;
        }

        void QueueRenameSceneAsset(
            const AssetRecord& record,
            std::string& renameSceneGuid,
            std::array<char, 128>& renameSceneNameBuffer) {

            renameSceneGuid = record.guid.value;
            const std::string name = record.displayName.empty()
                ? GetSceneAssetBaseName(record.sourcePath)
                : record.displayName;
            renameSceneNameBuffer.fill('\0');
            std::snprintf(renameSceneNameBuffer.data(), renameSceneNameBuffer.size(), "%s", name.c_str());
            ImGui::OpenPopup("Rename Scene Asset");
        }

        void QueueDeleteSceneAsset(
            const AssetRecord& record,
            std::string& deleteSceneGuid) {

            deleteSceneGuid = record.guid.value;
            ImGui::OpenPopup("Delete Scene Asset");
        }

        void QueueModelCookSettings(
            const AssetRecord& record,
            std::string& modelCookSettingsGuid,
            std::string& modelCookSettingsOriginalJson) {

            modelCookSettingsGuid = record.guid.value;
            modelCookSettingsOriginalJson = record.meta.importSettingsJson;
            ImGui::OpenPopup("Model Cook Settings");
        }

        void DrawAssetDragSource(const AssetRecord& record) {
            EDITOR::BeginAssetDragSource(record);
        }

        void DrawRecordContextMenu(
            AssetDatabase& assetDatabase,
            const AssetRecord& record,
            EditorSelection& selection,
            std::string& lastOperationMessage,
            const AssetBrowserContext* context,
            std::string& activatedSceneGuid,
            std::string& activatedSequenceGuid,
            std::string& saveSceneAsGuid,
            std::string& refreshRuntimeAssetGuid,
            std::string& reimportAndRefreshRuntimeAssetGuid,
            std::string& modelCookSettingsGuid,
            std::string& modelCookSettingsOriginalJson,
            std::string& renameSceneGuid,
            std::string& deleteSceneGuid,
            std::array<char, 128>& renameSceneNameBuffer) {

            if (record.type == AssetType::Scene) {
                if (ImGui::MenuItem("Open Scene")) {
                    SelectRecord(record, selection);
                    activatedSceneGuid = record.guid.value;
                    lastOperationMessage = "Scene open requested: " + record.displayName;
                    LogSceneAssetInfo("open requested: " + record.sourcePath.generic_string());
                }
                if (ImGui::MenuItem("Save Current Scene Here")) {
                    saveSceneAsGuid = record.guid.value;
                    lastOperationMessage = "Scene save requested: " + record.displayName;
                    LogSceneAssetInfo("save requested: " + record.sourcePath.generic_string());
                }
                if (ImGui::MenuItem("Duplicate Scene")) {
                    std::filesystem::path duplicatedPath{};
                    std::string error{};
                    if (DuplicateSceneAsset(assetDatabase, record, duplicatedPath, error)) {
                        assetDatabase.ScanAssets(true);
                        if (const AssetRecord* duplicated = assetDatabase.FindByPath(duplicatedPath)) {
                            SelectRecord(*duplicated, selection);
                        }
                        lastOperationMessage = "Scene duplicated";
                    } else {
                        lastOperationMessage = error.empty() ? "Scene duplicate failed" : error;
                    }
                }
                if (ImGui::MenuItem("Rename Scene")) {
                    QueueRenameSceneAsset(record, renameSceneGuid, renameSceneNameBuffer);
                }
                const bool isCurrentScene = context && context->currentSceneGuid == record.guid;
                if (ImGui::MenuItem("Delete Scene", nullptr, false, !isCurrentScene)) {
                    QueueDeleteSceneAsset(record, deleteSceneGuid);
                }
                if (isCurrentScene && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Open another scene before deleting this one.");
                }
                if (ImGui::MenuItem("Set as Startup Scene")) {
                    ProjectSettingsService settings{};
                    settings.Load(assetDatabase.GetProjectRoot());
                    if (settings.SetStartupSceneGuid(record.guid) && settings.Save()) {
                        lastOperationMessage = "Startup scene set: " + record.displayName;
                        LogSceneAssetInfo("set startup scene: " + record.sourcePath.generic_string());
                    } else {
                        lastOperationMessage = "Startup scene update failed";
                        LogSceneAssetWarn("set startup scene failed: " + record.sourcePath.generic_string());
                    }
                }
                ImGui::Separator();
            }

            if (record.type == AssetType::Model) {
                if (ImGui::MenuItem("Model Cook Settings...")) {
                    SelectRecord(record, selection);
                    QueueModelCookSettings(record, modelCookSettingsGuid, modelCookSettingsOriginalJson);
                    lastOperationMessage = "Model cook settings opened";
                }
                ImGui::Separator();
            }

            if (record.type == AssetType::Sequence) {
                if (ImGui::MenuItem("Open in Cinematics")) {
                    SelectRecord(record, selection);
                    activatedSequenceGuid = record.guid.value;
                    lastOperationMessage =
                        "Sequence open requested: " + record.displayName;
                }
                ImGui::Separator();
            }

            if (ImGui::MenuItem("Reimport")) {
                SelectRecord(record, selection);
                const bool ok = assetDatabase.ImportAsset(record.guid);
                lastOperationMessage = ok ? "Reimport succeeded" : "Reimport failed";
            }
            if (ImGui::MenuItem("Reimport + Refresh Runtime")) {
                SelectRecord(record, selection);
                const bool ok = assetDatabase.ImportAsset(record.guid);
                if (ok) {
                    reimportAndRefreshRuntimeAssetGuid = record.guid.value;
                }
                lastOperationMessage = ok ? "Reimported; runtime refresh queued" : "Reimport failed";
            }
            if (ImGui::MenuItem("Refresh Runtime Only")) {
                SelectRecord(record, selection);
                refreshRuntimeAssetGuid = record.guid.value;
                lastOperationMessage = "Runtime refresh queued";
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
            std::string& lastOperationMessage,
            const AssetBrowserContext* context,
            std::string& activatedSceneGuid,
            std::string& activatedSequenceGuid,
            std::string& saveSceneAsGuid,
            std::string& refreshRuntimeAssetGuid,
            std::string& reimportAndRefreshRuntimeAssetGuid,
            std::string& modelCookSettingsGuid,
            std::string& modelCookSettingsOriginalJson,
            std::string& renameSceneGuid,
            std::string& deleteSceneGuid,
            std::array<char, 128>& renameSceneNameBuffer) {

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
                    DisplayNameWithSceneBadges(*record, context) + "##" +
                    record->sourcePath.generic_string();
                if (ImGui::Selectable(label.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                    SelectRecord(*record, selection);
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        HandleRecordActivated(
                            *record,
                            lastOperationMessage,
                            activatedSceneGuid,
                            activatedSequenceGuid);
                    }
                }
                const bool rowHovered = ImGui::IsItemHovered();
                DrawAssetDragSource(*record);
                if (rowHovered) {
                    DrawRecordTooltip(*record);
                }

                if (ImGui::BeginPopupContextItem()) {
                    DrawRecordContextMenu(
                        assetDatabase,
                        *record,
                        selection,
                        lastOperationMessage,
                        context,
                        activatedSceneGuid,
                        activatedSequenceGuid,
                        saveSceneAsGuid,
                        refreshRuntimeAssetGuid,
                        reimportAndRefreshRuntimeAssetGuid,
                        modelCookSettingsGuid,
                        modelCookSettingsOriginalJson,
                        renameSceneGuid,
                        deleteSceneGuid,
                        renameSceneNameBuffer);
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
            std::string& lastOperationMessage,
            const AssetBrowserContext* context,
            std::string& activatedSceneGuid,
            std::string& activatedSequenceGuid,
            std::string& saveSceneAsGuid,
            std::string& refreshRuntimeAssetGuid,
            std::string& reimportAndRefreshRuntimeAssetGuid,
            std::string& modelCookSettingsGuid,
            std::string& modelCookSettingsOriginalJson,
            std::string& renameSceneGuid,
            std::string& deleteSceneGuid,
            std::array<char, 128>& renameSceneNameBuffer) {

            if (!ImGui::BeginTable(
                "AssetBrowserCompactRows",
                2,
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_SizingStretchProp)) {
                return;
            }

            ImGui::TableSetupColumn("Asset", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("Badges", ImGuiTableColumnFlags_WidthFixed, 230.0f);

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
                const std::string displayName = DisplayNameWithSceneBadges(*record, context);
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
                    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                        HandleRecordActivated(
                            *record,
                            lastOperationMessage,
                            activatedSceneGuid,
                            activatedSequenceGuid);
                    }
                }
                const bool rowHovered = ImGui::IsItemHovered();
                DrawAssetDragSource(*record);
                if (rowHovered) {
                    DrawRecordTooltip(*record);
                }
                if (ImGui::BeginPopupContextItem()) {
                    DrawRecordContextMenu(
                        assetDatabase,
                        *record,
                        selection,
                        lastOperationMessage,
                        context,
                        activatedSceneGuid,
                        activatedSequenceGuid,
                        saveSceneAsGuid,
                        refreshRuntimeAssetGuid,
                        reimportAndRefreshRuntimeAssetGuid,
                        modelCookSettingsGuid,
                        modelCookSettingsOriginalJson,
                        renameSceneGuid,
                        deleteSceneGuid,
                        renameSceneNameBuffer);
                    ImGui::EndPopup();
                }

                const AssetImportState state = GetImportState(*record);
                const char* usageBadge = ToCompactUsageBadge(*record, usageSummary);
                const char* cookedBadge = ToCompactCookedBadge(*record);

                ImGui::TableSetColumnIndex(1);
                DrawCompactBadge(ToCompactTypeBadge(record->type), TypeBadgeColor(record->type));
                ImGui::SameLine(0.0f, 6.0f);
                DrawCompactBadge(usageBadge, UsageBadgeColor(usageBadge));
                ImGui::SameLine(0.0f, 6.0f);
                DrawCompactBadge(ToCompactStateBadge(state), StateColor(state));
                ImGui::SameLine(0.0f, 6.0f);
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
            std::string& lastOperationMessage,
            const AssetBrowserContext* context,
            std::string& activatedSceneGuid,
            std::string& activatedSequenceGuid,
            std::string& saveSceneAsGuid,
            std::string& refreshRuntimeAssetGuid,
            std::string& reimportAndRefreshRuntimeAssetGuid,
            std::string& modelCookSettingsGuid,
            std::string& modelCookSettingsOriginalJson,
            std::string& renameSceneGuid,
            std::string& deleteSceneGuid,
            std::array<char, 128>& renameSceneNameBuffer) {

            (void)usageSummary;

            const float cardWidth = 142.0f;
            const float cardHeight = 132.0f;
            const float iconSize = 58.0f;
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

                const ImVec2 cardMin = ImGui::GetCursorScreenPos();
                ImGui::InvisibleButton(
                    "##AssetGridCard",
                    ImVec2(cardWidth, cardHeight),
                    ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
                const bool cardHovered = ImGui::IsItemHovered();
                const bool cardClicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
                const bool cardDoubleClicked = cardHovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left);
                const ImVec2 cardMax = ImGui::GetItemRectMax();
                const ImVec2 cursorAfterCard = ImGui::GetCursorScreenPos();

                if (cardClicked) {
                    SelectRecord(*record, selection);
                }
                DrawAssetDragSource(*record);
                if (cardHovered) {
                    DrawRecordTooltip(*record);
                }
                if (cardDoubleClicked) {
                    SelectRecord(*record, selection);
                    HandleRecordActivated(
                        *record,
                        lastOperationMessage,
                        activatedSceneGuid,
                        activatedSequenceGuid);
                }
                if (ImGui::BeginPopupContextItem()) {
                    DrawRecordContextMenu(
                        assetDatabase,
                        *record,
                        selection,
                        lastOperationMessage,
                        context,
                        activatedSceneGuid,
                        activatedSequenceGuid,
                        saveSceneAsGuid,
                        refreshRuntimeAssetGuid,
                        reimportAndRefreshRuntimeAssetGuid,
                        modelCookSettingsGuid,
                        modelCookSettingsOriginalJson,
                        renameSceneGuid,
                        deleteSceneGuid,
                        renameSceneNameBuffer);
                    ImGui::EndPopup();
                }

                const ImVec4 baseColor = isSelected
                    ? ImVec4(0.16f, 0.30f, 0.46f, 1.0f)
                    : (cardHovered ? ImVec4(0.16f, 0.21f, 0.27f, 1.0f) : ImVec4(0.11f, 0.14f, 0.18f, 1.0f));
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(
                    cardMin,
                    cardMax,
                    ImGui::GetColorU32(baseColor),
                    6.0f);
                drawList->AddRect(
                    cardMin,
                    cardMax,
                    ImGui::GetColorU32(isSelected ? ImVec4(0.36f, 0.62f, 0.92f, 1.0f) : ImVec4(0.22f, 0.27f, 0.34f, 1.0f)),
                    6.0f);

                const AssetImportState state = GetImportState(*record);
                drawList->AddCircleFilled(
                    ImVec2(cardMax.x - 13.0f, cardMin.y + 13.0f),
                    4.0f,
                    ImGui::GetColorU32(StateColor(state)));

                const ImVec2 iconPos{
                    cardMin.x + (cardWidth - iconSize) * 0.5f,
                    cardMin.y + 15.0f
                };
                ImGui::SetCursorScreenPos(iconPos);
                DrawAssetTypeIcon(record->type, ImVec2(iconSize, iconSize));

                const std::string displayName = record->displayName.empty()
                    ? record->sourcePath.stem().string()
                    : record->displayName;
                const float labelWidth = cardWidth - 20.0f;
                const std::string gridLabel = BuildGridCardLabel(displayName, labelWidth);
                ImGui::SetCursorScreenPos(ImVec2(cardMin.x + 10.0f, cardMin.y + iconSize + 27.0f));
                ImGui::TextUnformatted(gridLabel.c_str());
                ImGui::SetCursorScreenPos(cursorAfterCard);

                ImGui::PopID();
                column = (column + 1) % columns;
            }

            ImGui::EndTable();
        }

        void DrawSceneAssetModals(
            AssetDatabase& assetDatabase,
            EditorSelection& selection,
            std::string& lastOperationMessage,
            const AssetBrowserContext* context,
            std::string& renameSceneGuid,
            std::string& deleteSceneGuid,
            std::array<char, 128>& renameSceneNameBuffer) {

            bool renameOpen = true;
            if (ImGui::BeginPopupModal("Rename Scene Asset", &renameOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
                ImGui::TextUnformatted("Rename Scene Asset");
                ImGui::SetNextItemWidth(280.0f);
                ImGui::InputText("Name", renameSceneNameBuffer.data(), renameSceneNameBuffer.size());
                ImGui::Separator();

                if (ImGui::Button("Apply", ImVec2(96.0f, 0.0f))) {
                    AssetRecord* record = assetDatabase.FindByGuid(AssetGuid{ renameSceneGuid });
                    std::filesystem::path renamedPath{};
                    std::string error{};
                    if (record && RenameSceneAsset(assetDatabase, *record, renameSceneNameBuffer.data(), renamedPath, error)) {
                        assetDatabase.ScanAssets(true);
                        if (const AssetRecord* renamed = assetDatabase.FindByPath(renamedPath)) {
                            SelectRecord(*renamed, selection);
                        }
                        lastOperationMessage = "Scene renamed";
                    } else {
                        lastOperationMessage = error.empty() ? "Scene rename failed" : error;
                    }
                    renameSceneGuid.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f))) {
                    renameSceneGuid.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }

            bool deleteOpen = true;
            if (ImGui::BeginPopupModal("Delete Scene Asset", &deleteOpen, ImGuiWindowFlags_AlwaysAutoResize)) {
                const AssetRecord* record = assetDatabase.FindByGuid(AssetGuid{ deleteSceneGuid });
                ImGui::TextUnformatted("Delete Scene Asset?");
                ImGui::TextDisabled("The source and meta file will be moved to Library/Trash.");
                if (record) {
                    ImGui::TextWrapped("%s", record->sourcePath.generic_string().c_str());
                }
                const bool deletingStartup = record && context && context->startupSceneGuid == record->guid;
                if (deletingStartup) {
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.72f, 0.34f, 1.0f),
                        "This scene is the startup scene. Deleting it will clear startupSceneGuid.");
                }
                ImGui::Separator();

                const bool deletingCurrent = record && context && context->currentSceneGuid == record->guid;
                if (deletingCurrent) {
                    ImGui::TextColored(ImVec4(1.0f, 0.46f, 0.34f, 1.0f), "Open another scene before deleting this one.");
                    ImGui::BeginDisabled();
                }
                if (ImGui::Button("Delete", ImVec2(96.0f, 0.0f))) {
                    std::string error{};
                    bool clearedStartupScene = false;
                    if (record && DeleteSceneAssetToTrash(assetDatabase, *record, clearedStartupScene, error)) {
                        assetDatabase.ScanAssets(true);
                        selection.selectedAssetGuid.clear();
                        selection.selectedAssetPath.clear();
                        lastOperationMessage = clearedStartupScene
                            ? "Scene moved to Library/Trash; startup scene cleared"
                            : "Scene moved to Library/Trash";
                    } else {
                        lastOperationMessage = error.empty() ? "Scene delete failed" : error;
                    }
                    deleteSceneGuid.clear();
                    ImGui::CloseCurrentPopup();
                }
                if (deletingCurrent) {
                    ImGui::EndDisabled();
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel", ImVec2(96.0f, 0.0f))) {
                    deleteSceneGuid.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
            }
        }

        void DrawModelCookSettingsModal(
            AssetDatabase& assetDatabase,
            std::string& lastOperationMessage,
            std::string& reimportAndRefreshRuntimeAssetGuid,
            std::string& modelCookSettingsGuid,
            std::string& modelCookSettingsOriginalJson) {

            bool open = true;
            if (!ImGui::BeginPopupModal("Model Cook Settings", &open, ImGuiWindowFlags_AlwaysAutoResize)) {
                return;
            }

            AssetRecord* record = assetDatabase.FindByGuid(AssetGuid{ modelCookSettingsGuid });
            if (!record || record->type != AssetType::Model) {
                ImGui::TextDisabled("Selected model is no longer available");
                if (ImGui::Button("Close", ImVec2(96.0f, 0.0f))) {
                    modelCookSettingsGuid.clear();
                    modelCookSettingsOriginalJson.clear();
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndPopup();
                return;
            }

            ImGui::Text("%s", record->displayName.c_str());
            ImGui::TextDisabled("%s", record->sourcePath.generic_string().c_str());
            ImGui::Separator();

            nlohmann::json settings = ReadImportSettings(*record);
            if (DrawModelClusterCookSettings(settings)) {
                record->meta.importSettingsJson = settings.dump(2);
            }

            auto saveMeta = [&]() -> bool {
                if (!assetDatabase.WriteMeta(*record)) {
                    lastOperationMessage = "Model cook settings save failed";
                    return false;
                }
                record->importOutdated = true;
                return true;
            };

            ImGui::Separator();
            if (ImGui::Button("Save", ImVec2(110.0f, 0.0f))) {
                if (saveMeta()) {
                    lastOperationMessage = "Model cook settings saved";
                    modelCookSettingsGuid.clear();
                    modelCookSettingsOriginalJson.clear();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reimport", ImVec2(110.0f, 0.0f))) {
                const bool saved = saveMeta();
                const bool imported = saved && assetDatabase.ImportAsset(record->guid);
                lastOperationMessage = imported ? "Model reimport succeeded" : "Model reimport failed";
                if (imported) {
                    modelCookSettingsGuid.clear();
                    modelCookSettingsOriginalJson.clear();
                    ImGui::CloseCurrentPopup();
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reimport + Runtime", ImVec2(150.0f, 0.0f))) {
                const bool saved = saveMeta();
                const bool imported = saved && assetDatabase.ImportAsset(record->guid);
                if (imported) {
                    reimportAndRefreshRuntimeAssetGuid = record->guid.value;
                    lastOperationMessage = "Model reimported; runtime refresh queued";
                    modelCookSettingsGuid.clear();
                    modelCookSettingsOriginalJson.clear();
                    ImGui::CloseCurrentPopup();
                } else {
                    lastOperationMessage = "Model reimport failed";
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(110.0f, 0.0f))) {
                record->meta.importSettingsJson = modelCookSettingsOriginalJson;
                modelCookSettingsGuid.clear();
                modelCookSettingsOriginalJson.clear();
                ImGui::CloseCurrentPopup();
            }

            if (!open) {
                record->meta.importSettingsJson = modelCookSettingsOriginalJson;
                modelCookSettingsGuid.clear();
                modelCookSettingsOriginalJson.clear();
            }
            ImGui::EndPopup();
        }
#endif
    }

    void AssetBrowserPanel::Draw(AssetDatabase& assetDatabase, EditorSelection& selection) const {
#if defined(HIKARI_WITH_EDITOR)
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
        AssetBrowserScope scope,
        const AssetBrowserContext* context) const {
#if defined(HIKARI_WITH_EDITOR)
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
        if (scope == AssetBrowserScope::Scenes) {
            ImGui::TextDisabled("Scenes are project assets. Open, duplicate, delete, and set startup scene here.");
            if (context) {
                ImGui::TextDisabled("Current: %s    Startup: %s",
                    SceneDisplayNameByGuid(assetDatabase, context->currentSceneGuid).c_str(),
                    SceneDisplayNameByGuid(assetDatabase, context->startupSceneGuid).c_str());
            }
            ImGui::Separator();
        }

        if (ImGui::Button("+ New")) {
            ImGui::OpenPopup("AssetBrowserCreateMenu");
        }
        if (ImGui::BeginPopup("AssetBrowserCreateMenu")) {
            // 闖ｴ諛医・驍会ｽｻ邵ｺ・ｯ郢晢ｽ｡郢ｧ・､郢晢ｽｳ郢晁・繝ｻ邵ｺ荵晢ｽ蛾ｨｾ繝ｻ窶ｲ邵ｺ蜉ｱﾂ竏壹＆郢晢ｽｳ郢昴・ﾎｦ郢昴・・ｰ莨懈ｲｺ邵ｺ・ｮ隴√・ﾑ・ｬｫ蝣ｺ・ｽ諛岩・邵ｺ蜉ｱ窶ｻ隰・ｽｱ邵ｺ繝ｻﾂ繝ｻ
            if (ImGui::MenuItem("Folder")) {
                CreateFolderFromBrowser(assetDatabase, currentDirectory_, lastOperationMessage_);
            }
            if (ImGui::MenuItem("Scene Asset")) {
                CreateSceneFromBrowser(assetDatabase, currentDirectory_, selection, lastOperationMessage_);
            }
            if (ImGui::MenuItem("Material Asset")) {
                CreateMaterialFromBrowser(assetDatabase, currentDirectory_, selection, lastOperationMessage_);
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine();
        static const char* ViewModeItems[] = { "Compact", "List", "Grid" };
        ImGui::SetNextItemWidth(104.0f);
        ImGui::Combo("##AssetViewMode", &viewMode_, ViewModeItems, IM_ARRAYSIZE(ViewModeItems));
        ImGui::SameLine();
        if (ImGui::SmallButton(filtersExpanded_ ? "Hide Filters" : "Filters")) {
            filtersExpanded_ = !filtersExpanded_;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Recursive", &recursive_);

        if (filtersExpanded_ || searchBuffer_[0] != '\0' || typeFilter_ != 0 || stateFilter_ != 0) {
            ImGui::SetNextItemWidth((std::max)(220.0f, ImGui::GetContentRegionAvail().x * 0.42f));
            ImGui::InputTextWithHint("##AssetSearch", "Search assets...", searchBuffer_.data(), searchBuffer_.size());
            ImGui::SameLine();
            static const char* TypeFilterItems[] = { "All", "Texture", "Model", "Scene", "Sky", "Material", "VFX", "Sequence" };
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
        }
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
                EDITOR::EditorIconManager::DrawIcon(EDITOR::EditorIconKind::Folder, ImVec2(15.0f, 15.0f));
                ImGui::SameLine();
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
        if (ImGui::BeginPopupContextWindow(
            "AssetBrowserEmptyContext",
            ImGuiPopupFlags_MouseButtonRight | ImGuiPopupFlags_NoOpenOverItems)) {
            if (ImGui::MenuItem("New Folder")) {
                CreateFolderFromBrowser(assetDatabase, currentDirectory_, lastOperationMessage_);
            }
            if (ImGui::MenuItem("New Scene Asset")) {
                CreateSceneFromBrowser(assetDatabase, currentDirectory_, selection, lastOperationMessage_);
            }
            if (ImGui::MenuItem("New Material Asset")) {
                CreateMaterialFromBrowser(assetDatabase, currentDirectory_, selection, lastOperationMessage_);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Refresh Assets")) {
                const bool ok = assetDatabase.ScanAssets(true);
                lastOperationMessage_ = ok ? "AssetDatabase refreshed" : "AssetDatabase refresh failed";
            }
            ImGui::EndPopup();
        }

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
            DrawRecordList(
                assetDatabase,
                records,
                selection,
                usageSummary,
                lastOperationMessage_,
                context,
                activatedSceneGuid_,
                activatedSequenceGuid_,
                saveSceneAsGuid_,
                refreshRuntimeAssetGuid_,
                reimportAndRefreshRuntimeAssetGuid_,
                modelCookSettingsGuid_,
                modelCookSettingsOriginalJson_,
                renameSceneGuid_,
                deleteSceneGuid_,
                renameSceneNameBuffer_);
        } else if (viewMode_ == 2) {
            DrawRecordGrid(
                assetDatabase,
                records,
                selection,
                usageSummary,
                lastOperationMessage_,
                context,
                activatedSceneGuid_,
                activatedSequenceGuid_,
                saveSceneAsGuid_,
                refreshRuntimeAssetGuid_,
                reimportAndRefreshRuntimeAssetGuid_,
                modelCookSettingsGuid_,
                modelCookSettingsOriginalJson_,
                renameSceneGuid_,
                deleteSceneGuid_,
                renameSceneNameBuffer_);
        } else {
            DrawRecordCompactRows(
                assetDatabase,
                records,
                selection,
                usageSummary,
                lastOperationMessage_,
                context,
                activatedSceneGuid_,
                activatedSequenceGuid_,
                saveSceneAsGuid_,
                refreshRuntimeAssetGuid_,
                reimportAndRefreshRuntimeAssetGuid_,
                modelCookSettingsGuid_,
                modelCookSettingsOriginalJson_,
                renameSceneGuid_,
                deleteSceneGuid_,
                renameSceneNameBuffer_);
        }

        ImGui::EndChild();
        DrawSceneAssetModals(
            assetDatabase,
            selection,
            lastOperationMessage_,
            context,
            renameSceneGuid_,
            deleteSceneGuid_,
            renameSceneNameBuffer_);
        DrawModelCookSettingsModal(
            assetDatabase,
            lastOperationMessage_,
            reimportAndRefreshRuntimeAssetGuid_,
            modelCookSettingsGuid_,
            modelCookSettingsOriginalJson_);
#else
        (void)assetDatabase;
        (void)selection;
        (void)usageSummary;
        (void)scope;
        (void)context;
#endif
    }

    std::string AssetBrowserPanel::ConsumeActivatedSceneGuid() const {
#if defined(HIKARI_WITH_EDITOR)
        std::string value = std::move(activatedSceneGuid_);
        activatedSceneGuid_.clear();
        return value;
#else
        return {};
#endif
    }

    std::string AssetBrowserPanel::ConsumeActivatedSequenceGuid() const {
#if defined(HIKARI_WITH_EDITOR)
        std::string value = std::move(activatedSequenceGuid_);
        activatedSequenceGuid_.clear();
        return value;
#else
        return {};
#endif
    }

    std::string AssetBrowserPanel::ConsumeSaveSceneAsGuid() const {
#if defined(HIKARI_WITH_EDITOR)
        std::string value = std::move(saveSceneAsGuid_);
        saveSceneAsGuid_.clear();
        return value;
#else
        return {};
#endif
    }

    std::string AssetBrowserPanel::ConsumeRefreshRuntimeAssetGuid() const {
#if defined(HIKARI_WITH_EDITOR)
        std::string value = std::move(refreshRuntimeAssetGuid_);
        refreshRuntimeAssetGuid_.clear();
        return value;
#else
        return {};
#endif
    }

    std::string AssetBrowserPanel::ConsumeReimportAndRefreshRuntimeAssetGuid() const {
#if defined(HIKARI_WITH_EDITOR)
        std::string value = std::move(reimportAndRefreshRuntimeAssetGuid_);
        reimportAndRefreshRuntimeAssetGuid_.clear();
        return value;
#else
        return {};
#endif
    }

    std::filesystem::path AssetBrowserPanel::CurrentDirectory() const {
        return currentDirectory_.empty() ? std::filesystem::path("Assets") : currentDirectory_;
    }

    bool AssetBrowserPanel::IsRecursiveEnabled() const {
        return recursive_;
    }

} // namespace HIKARI
