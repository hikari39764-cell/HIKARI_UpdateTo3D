#include "HIKARI_AssetDatabase.h"

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unordered_set>

#include <json.hpp>

#include "Core/HIKARI_Logger.h"
#include "Importers/HIKARI_MaterialImporterStub.h"
#include "Importers/HIKARI_ModelImporterStub.h"
#include "Importers/HIKARI_SceneImporterStub.h"
#include "Importers/HIKARI_SkyCubemapImporter.h"
#include "Importers/HIKARI_TextureImportBackend_DirectXTex.h"
#include "Importers/HIKARI_TextureImporter.h"
#include "Importers/HIKARI_VfxImporterStub.h"

namespace HIKARI {

    namespace {
        std::string ToLowerCopy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        bool EndsWith(std::string_view text, std::string_view suffix) {
            return text.size() >= suffix.size() &&
                text.substr(text.size() - suffix.size()) == suffix;
        }

        bool IsIgnoredDirectoryName(const std::string& name) {
            const std::string lower = ToLowerCopy(name);
            return lower == ".git" ||
                lower == "library" ||
                lower == "projectsettings" ||
                lower == "tmp" ||
                lower == "cache";
        }

        bool IsMetaPath(const std::filesystem::path& path) {
            return EndsWith(ToLowerCopy(path.filename().string()), ".hikari.meta");
        }

        bool IsSkyFolderPath(const std::filesystem::path& path) {
            for (const std::filesystem::path& part : path) {
                const std::string lower = ToLowerCopy(part.string());
                if (lower == "skies" || lower == "sky") {
                    return true;
                }
            }
            return false;
        }

        bool IsTextureExtension(const std::string& ext) {
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                ext == ".tga" || ext == ".bmp" || ext == ".dds" ||
                ext == ".hdr";
        }

        bool IsModelExtension(const std::string& ext) {
            return ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".obj";
        }

        bool IsVfxExtension(const std::string& ext) {
            return ext == ".efk" || ext == ".efkefc";
        }

        bool IsScenePath(const std::filesystem::path& path) {
            const std::string filename = ToLowerCopy(path.filename().string());
            const std::string ext = ToLowerCopy(path.extension().string());
            const std::string generic = ToLowerCopy(path.generic_string());
            return ext == ".hscene" ||
                EndsWith(filename, ".scene.json") ||
                (ext == ".json" && generic.find("assets/scenes/") != std::string::npos);
        }

        const char* ToString(AssetType type) {
            switch (type) {
            case AssetType::Model: return "Model";
            case AssetType::Scene: return "Scene";
            case AssetType::Sky: return "Sky";
            case AssetType::Texture: return "Texture";
            case AssetType::Material: return "Material";
            case AssetType::Animation: return "Animation";
            case AssetType::Particle: return "Particle";
            case AssetType::VfxEffect: return "VfxEffect";
            case AssetType::Unknown:
            default: return "Unknown";
            }
        }

        AssetType ParseAssetType(const std::string& text) {
            if (text == "Model") return AssetType::Model;
            if (text == "Scene") return AssetType::Scene;
            if (text == "Sky") return AssetType::Sky;
            if (text == "Texture") return AssetType::Texture;
            if (text == "Material") return AssetType::Material;
            if (text == "Animation") return AssetType::Animation;
            if (text == "Particle") return AssetType::Particle;
            if (text == "VfxEffect" || text == "Vfx") return AssetType::VfxEffect;
            return AssetType::Unknown;
        }

        nlohmann::json SerializeDependencies(const std::vector<AssetDependencyDesc>& dependencies) {
            nlohmann::json out = nlohmann::json::array();
            for (const AssetDependencyDesc& dependency : dependencies) {
                out.push_back({
                    { "guid", dependency.guid.value },
                    { "path", dependency.path },
                    { "role", dependency.role },
                });
            }
            return out;
        }

        nlohmann::json SerializeArtifacts(const std::vector<AssetArtifactDesc>& artifacts) {
            nlohmann::json out = nlohmann::json::array();
            for (const AssetArtifactDesc& artifact : artifacts) {
                out.push_back({
                    { "role", artifact.role },
                    { "path", artifact.path },
                    { "format", artifact.format },
                });
            }
            return out;
        }

        void ParseDependencies(const nlohmann::json& root, std::vector<AssetDependencyDesc>& out) {
            out.clear();
            if (!root.contains("dependencies") || !root["dependencies"].is_array()) {
                return;
            }

            for (const nlohmann::json& node : root["dependencies"]) {
                if (!node.is_object()) {
                    continue;
                }
                AssetDependencyDesc dependency{};
                dependency.guid.value = node.value("guid", "");
                dependency.path = node.value("path", "");
                dependency.role = node.value("role", "");
                out.push_back(std::move(dependency));
            }
        }

        void ParseArtifacts(const nlohmann::json& root, std::vector<AssetArtifactDesc>& out) {
            out.clear();
            if (!root.contains("artifacts") || !root["artifacts"].is_array()) {
                return;
            }

            for (const nlohmann::json& node : root["artifacts"]) {
                if (!node.is_object()) {
                    continue;
                }
                AssetArtifactDesc artifact{};
                artifact.role = node.value("role", "");
                artifact.path = node.value("path", "");
                artifact.format = node.value("format", "");
                out.push_back(std::move(artifact));
            }
        }

        std::filesystem::path ResolveProjectPath(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& path) {

            if (path.empty()) {
                return {};
            }
            if (path.is_absolute()) {
                return path.lexically_normal();
            }
            return (projectRoot / path).lexically_normal();
        }

        bool ReadJsonFile(const std::filesystem::path& path, nlohmann::json& outRoot) {
            std::ifstream ifs(path);
            if (!ifs.is_open()) {
                return false;
            }

            outRoot = nlohmann::json::parse(ifs, nullptr, false);
            return !outRoot.is_discarded() && outRoot.is_object();
        }
    }

    bool AssetDatabase::Initialize(const std::filesystem::path& projectRoot) {
        std::error_code ec{};
        const std::filesystem::path root = projectRoot.empty()
            ? std::filesystem::current_path(ec)
            : projectRoot;

        projectRoot_ = std::filesystem::absolute(root, ec).lexically_normal();
        if (ec) {
            projectRoot_ = root.lexically_normal();
        }

        assetsRoot_ = projectRoot_ / "Assets";
        libraryRoot_ = projectRoot_ / "Library";
        projectSettingsRoot_ = projectRoot_ / "ProjectSettings";

        RegisterDefaultImporters();
        EnsureProjectDirectories();
        return true;
    }

    const std::filesystem::path& AssetDatabase::GetProjectRoot() const {
        return projectRoot_;
    }

    const std::filesystem::path& AssetDatabase::GetAssetsRoot() const {
        return assetsRoot_;
    }

    const std::filesystem::path& AssetDatabase::GetLibraryRoot() const {
        return libraryRoot_;
    }

    AssetImporterRegistry& AssetDatabase::GetImporterRegistry() {
        return importerRegistry_;
    }

    const AssetImporterRegistry& AssetDatabase::GetImporterRegistry() const {
        return importerRegistry_;
    }

    bool AssetDatabase::ScanAssets(bool createMissingMeta) {
        EnsureProjectDirectories();

        records_.clear();
        directories_.clear();
        recordsByGuid_.clear();
        guidByNormalizedPath_.clear();
        AddDirectoryToCache("Assets");

        std::error_code ec{};
        if (!std::filesystem::exists(assetsRoot_, ec)) {
            HIKARI_LOG_WARN("[AssetDatabase] Assets root missing: " + assetsRoot_.generic_string());
            return false;
        }

        std::filesystem::recursive_directory_iterator it(
            assetsRoot_,
            std::filesystem::directory_options::skip_permission_denied,
            ec);
        const std::filesystem::recursive_directory_iterator end{};
        for (; !ec && it != end; it.increment(ec)) {
            const std::filesystem::directory_entry& entry = *it;
            if (entry.is_directory(ec)) {
                if (IsIgnoredDirectoryName(entry.path().filename().string())) {
                    it.disable_recursion_pending();
                    continue;
                }

                std::error_code relativeEc{};
                const std::filesystem::path relativeDirectory =
                    std::filesystem::relative(entry.path(), projectRoot_, relativeEc);
                if (!relativeEc) {
                    AddDirectoryToCache(relativeDirectory.lexically_normal());
                }
                continue;
            }

            if (!entry.is_regular_file(ec)) {
                continue;
            }

            const std::filesystem::path& absoluteSource = entry.path();
            if (IsMetaPath(absoluteSource)) {
                continue;
            }

            std::filesystem::path relativeSource = NormalizeProjectPath(absoluteSource);
            const AssetType guessedType = GuessAssetTypeFromPath(relativeSource);
            const bool hasExistingMeta = std::filesystem::exists(GetMetaPathForSource(relativeSource), ec);
            if (guessedType == AssetType::Unknown && !hasExistingMeta) {
                continue;
            }

            AssetRecord record = BuildRecordForSource(relativeSource, createMissingMeta);
            const std::string pathKey = MakePathKey(record.sourcePath);
            const size_t index = records_.size();
            AddDirectoryToCache(record.sourcePath.parent_path());

            if (record.guid.IsValid()) {
                const auto existing = recordsByGuid_.find(record.guid.value);
                if (existing != recordsByGuid_.end()) {
                    record.duplicateGuid = true;
                    record.lastImportMessage = "[AssetDatabase] duplicate GUID: " + record.guid.value;
                    HIKARI_LOG_ERROR(record.lastImportMessage + " source=" + record.sourcePath.generic_string());
                } else {
                    recordsByGuid_[record.guid.value] = index;
                }
            }

            guidByNormalizedPath_[pathKey] = index;
            records_.push_back(std::move(record));
        }

        if (ec) {
            HIKARI_LOG_ERROR("[AssetDatabase] scan failed: " + ec.message());
            return false;
        }

        SortAndUniqueDirectories();
        HIKARI_LOG_INFO("[AssetDatabase] scanned assets. count=" + std::to_string(records_.size()));
        return true;
    }

    bool AssetDatabase::ImportAsset(const AssetGuid& guid) {
        AssetRecord* record = FindByGuid(guid);
        if (!record) {
            HIKARI_LOG_ERROR("[AssetDatabase] ImportAsset failed: GUID not found " + guid.value);
            return false;
        }

        if (!record->guid.IsValid()) {
            record->lastImportSucceeded = false;
            record->lastImportMessage = "[AssetDatabase] invalid GUID";
            return false;
        }

        if (!record->sourceExists) {
            AssetImportResult result{};
            result.message = "[AssetDatabase] source missing: " + record->sourcePath.generic_string();
            WriteImportReport(*record, result);
            RefreshRecordState(*record);
            return false;
        }

        IAssetImporter* importer = importerRegistry_.FindById(record->meta.importerId);
        if (!importer) {
            AssetImportResult result{};
            result.message = "[AssetDatabase] Unknown Importer: " + record->meta.importerId;
            WriteImportReport(*record, result);
            RefreshRecordState(*record);
            return false;
        }

        AssetImportContext context{};
        context.projectRoot = projectRoot_;
        context.assetsRoot = assetsRoot_;
        context.libraryRoot = libraryRoot_;
        context.importedDirectory = GetImportedDirectory(record->guid);

        std::error_code ec{};
        std::filesystem::create_directories(context.importedDirectory, ec);
        if (ec) {
            AssetImportResult result{};
            result.message = "[AssetDatabase] failed to create imported directory: " + context.importedDirectory.generic_string();
            WriteImportReport(*record, result);
            RefreshRecordState(*record);
            return false;
        }

        AssetImportResult result{};
        try {
            result = importer->Import(*record, context);
        } catch (const std::exception& ex) {
            result.success = false;
            result.message = std::string("[AssetDatabase] importer exception: ") + ex.what();
            HIKARI_LOG_ERROR(result.message + " source=" + record->sourcePath.generic_string() + " guid=" + record->guid.value);
        } catch (...) {
            result.success = false;
            result.message = "[AssetDatabase] importer exception: unknown";
            HIKARI_LOG_ERROR(result.message + " source=" + record->sourcePath.generic_string() + " guid=" + record->guid.value);
        }
        if (result.success) {
            record->meta.importerVersion = importer->GetImporterVersion();
            record->meta.sourcePath = record->sourcePath.generic_string();
            record->meta.displayName = record->displayName.empty()
                ? record->sourcePath.stem().string()
                : record->displayName;
            record->meta.artifacts = result.artifacts;
            record->meta.dependencies = result.dependencies;
            if (!WriteMeta(*record)) {
                result.success = false;
                result.message = "[AssetDatabase] import succeeded but meta write failed";
            }
        }

        WriteImportReport(*record, result);
        RefreshRecordState(*record);
        return result.success;
    }

    AssetImportBatchResult AssetDatabase::ImportAllOutdated() {
        AssetImportBatchResult batch{};
        std::vector<AssetGuid> importGuids;
        importGuids.reserve(records_.size());

        for (const AssetRecord& record : records_) {
            if (!record.importOutdated ||
                record.duplicateGuid ||
                !record.sourceExists ||
                record.importerMissing ||
                !record.guid.IsValid()) {
                continue;
            }
            importGuids.push_back(record.guid);
        }

        for (const AssetGuid& guid : importGuids) {
            ++batch.attempted;
            if (ImportAsset(guid)) {
                ++batch.succeeded;
            } else {
                ++batch.failed;
            }
        }

        return batch;
    }

    AssetImportBatchResult AssetDatabase::ImportDependencies(const AssetGuid& guid, bool includeSelf) {
        AssetImportBatchResult batch{};
        const AssetRecord* rootRecord = FindByGuid(guid);
        if (!rootRecord) {
            HIKARI_LOG_ERROR("[AssetDatabase] ImportDependencies failed: GUID not found " + guid.value);
            return batch;
        }

        std::vector<AssetGuid> importGuids;
        std::unordered_set<std::string> visited;
        importGuids.reserve(rootRecord->meta.dependencies.size() + (includeSelf ? 1u : 0u));

        auto queueGuid = [&](const AssetGuid& candidateGuid) {
            if (!candidateGuid.IsValid() || !visited.insert(candidateGuid.value).second) {
                return;
            }
            importGuids.push_back(candidateGuid);
        };

        for (const AssetDependencyDesc& dependency : rootRecord->meta.dependencies) {
            if (dependency.guid.IsValid()) {
                queueGuid(dependency.guid);
                continue;
            }
            if (!dependency.path.empty()) {
                if (const AssetRecord* dependencyRecord = FindByPath(dependency.path)) {
                    queueGuid(dependencyRecord->guid);
                }
            }
        }

        if (includeSelf) {
            queueGuid(rootRecord->guid);
        }

        for (const AssetGuid& dependencyGuid : importGuids) {
            AssetRecord* record = FindByGuid(dependencyGuid);
            if (!record ||
                record->duplicateGuid ||
                !record->sourceExists ||
                record->importerMissing ||
                !record->guid.IsValid()) {
                ++batch.failed;
                continue;
            }

            ++batch.attempted;
            if (ImportAsset(dependencyGuid)) {
                ++batch.succeeded;
            } else {
                ++batch.failed;
            }
        }

        return batch;
    }

    const AssetRecord* AssetDatabase::FindByGuid(const AssetGuid& guid) const {
        const auto it = recordsByGuid_.find(guid.value);
        if (it == recordsByGuid_.end() || it->second >= records_.size()) {
            return nullptr;
        }
        return &records_[it->second];
    }

    AssetRecord* AssetDatabase::FindByGuid(const AssetGuid& guid) {
        const auto it = recordsByGuid_.find(guid.value);
        if (it == recordsByGuid_.end() || it->second >= records_.size()) {
            return nullptr;
        }
        return &records_[it->second];
    }

    const AssetRecord* AssetDatabase::FindByPath(const std::filesystem::path& path) const {
        const auto it = guidByNormalizedPath_.find(MakePathKey(path));
        if (it == guidByNormalizedPath_.end() || it->second >= records_.size()) {
            return nullptr;
        }
        return &records_[it->second];
    }

    AssetRecord* AssetDatabase::FindByPath(const std::filesystem::path& path) {
        const auto it = guidByNormalizedPath_.find(MakePathKey(path));
        if (it == guidByNormalizedPath_.end() || it->second >= records_.size()) {
            return nullptr;
        }
        return &records_[it->second];
    }

    std::vector<const AssetRecord*> AssetDatabase::CollectByType(AssetType type) const {
        std::vector<const AssetRecord*> out;
        for (const AssetRecord& record : records_) {
            if (record.type == type) {
                out.push_back(&record);
            }
        }
        return out;
    }

    std::vector<const AssetRecord*> AssetDatabase::CollectAll() const {
        std::vector<const AssetRecord*> out;
        out.reserve(records_.size());
        for (const AssetRecord& record : records_) {
            out.push_back(&record);
        }
        return out;
    }

    std::vector<std::filesystem::path> AssetDatabase::CollectDirectories() const {
        return directories_;
    }

    std::vector<const AssetRecord*> AssetDatabase::CollectInDirectory(
        const std::filesystem::path& directory,
        bool recursive) const {

        std::vector<const AssetRecord*> out;
        for (const AssetRecord& record : records_) {
            const bool include = recursive
                ? IsPathUnderDirectory(record.sourcePath, directory)
                : MakePathKey(record.sourcePath.parent_path()) == MakePathKey(directory);
            if (include) {
                out.push_back(&record);
            }
        }
        return out;
    }

    bool AssetDatabase::WriteMeta(const AssetRecord& record) {
        if (record.metaPath.empty()) {
            return false;
        }

        std::error_code ec{};
        std::filesystem::create_directories(record.metaPath.parent_path(), ec);
        if (ec) {
            HIKARI_LOG_ERROR("[AssetDatabase] failed to create meta directory: " + record.metaPath.parent_path().generic_string());
            return false;
        }

        nlohmann::json settings = nlohmann::json::parse(record.meta.importSettingsJson, nullptr, false);
        if (!settings.is_object()) {
            settings = nlohmann::json::object();
        }

        nlohmann::json root{
            { "metaVersion", record.meta.metaVersion },
            { "guid", record.meta.guid.value },
            { "type", ToString(record.meta.type) },
            { "importerId", record.meta.importerId },
            { "importerVersion", record.meta.importerVersion },
            { "sourcePath", record.meta.sourcePath },
            { "displayName", record.meta.displayName },
            { "importSettings", settings },
            { "dependencies", SerializeDependencies(record.meta.dependencies) },
            { "artifacts", SerializeArtifacts(record.meta.artifacts) },
        };

        std::ofstream ofs(record.metaPath);
        if (!ofs.is_open()) {
            HIKARI_LOG_ERROR("[AssetDatabase] failed to open meta for write: " + record.metaPath.generic_string());
            return false;
        }
        ofs << root.dump(2) << '\n';
        return true;
    }

    bool AssetDatabase::ReadMeta(const std::filesystem::path& metaPath, AssetMeta& outMeta) const {
        const std::filesystem::path absoluteMetaPath = ResolveProjectPath(projectRoot_, metaPath);

        nlohmann::json root;
        if (!ReadJsonFile(absoluteMetaPath, root)) {
            HIKARI_LOG_ERROR("[AssetDatabase] meta JSON parse failed: " + absoluteMetaPath.generic_string());
            return false;
        }

        outMeta = AssetMeta{};
        outMeta.metaVersion = root.value("metaVersion", 1u);
        outMeta.guid.value = root.value("guid", "");
        outMeta.type = ParseAssetType(root.value("type", "Unknown"));
        outMeta.importerId = root.value("importerId", "");
        outMeta.importerVersion = root.value("importerVersion", 1u);
        outMeta.sourcePath = root.value("sourcePath", "");
        outMeta.displayName = root.value("displayName", "");
        if (root.contains("importSettings") && root["importSettings"].is_object()) {
            outMeta.importSettingsJson = root["importSettings"].dump(2);
        } else {
            outMeta.importSettingsJson = "{}";
        }
        ParseDependencies(root, outMeta.dependencies);
        ParseArtifacts(root, outMeta.artifacts);
        return true;
    }

    bool AssetDatabase::RegenerateMeta(const std::filesystem::path& sourcePath) {
        AssetRecord* record = FindByPath(sourcePath);
        if (!record) {
            HIKARI_LOG_ERROR("[AssetDatabase] RegenerateMeta failed: source not found " + sourcePath.generic_string());
            return false;
        }

        const AssetGuid guid = record->guid.IsValid() ? record->guid : GenerateAssetGuid();
        const AssetType type = GuessAssetTypeFromPath(record->sourcePath);
        const std::string importerId = SelectDefaultImporterId(type, record->sourcePath);

        AssetMeta meta{};
        if (const IAssetImporter* importer = importerRegistry_.FindById(importerId)) {
            meta = importer->CreateDefaultMeta(record->sourcePath, guid);
        } else {
            meta.guid = guid;
            meta.type = type;
            meta.importerId = importerId;
            meta.importerVersion = 1;
            meta.sourcePath = record->sourcePath.generic_string();
            meta.displayName = record->sourcePath.stem().string();
            meta.importSettingsJson = "{}";
        }

        record->guid = meta.guid;
        record->type = meta.type;
        record->meta = std::move(meta);
        record->displayName = record->meta.displayName;
        record->metaPath = GetMetaPathForSource(record->sourcePath);
        record->importedDirectory = GetImportedDirectory(record->guid);
        if (record->guid.IsValid()) {
            const size_t index = static_cast<size_t>(record - records_.data());
            recordsByGuid_[record->guid.value] = index;
            guidByNormalizedPath_[MakePathKey(record->sourcePath)] = index;
        }
        const bool wrote = WriteMeta(*record);
        RefreshRecordState(*record);
        record->importOutdated = true;
        record->lastImportMessage = wrote
            ? "[AssetDatabase] Meta regenerated; reimport required"
            : "[AssetDatabase] Meta regeneration failed";
        return wrote;
    }

    std::filesystem::path AssetDatabase::GetMetaPathForSource(const std::filesystem::path& sourcePath) const {
        const std::filesystem::path relativeSource = NormalizeProjectPath(sourcePath);
        const std::filesystem::path absoluteSource = ResolveProjectPath(projectRoot_, relativeSource);
        return absoluteSource.parent_path() / (absoluteSource.filename().string() + ".hikari.meta");
    }

    std::filesystem::path AssetDatabase::GetImportedDirectory(const AssetGuid& guid) const {
        if (!guid.IsValid()) {
            return {};
        }
        return libraryRoot_ / "Imported" / guid.value;
    }

    AssetRecord AssetDatabase::BuildRecordForSource(const std::filesystem::path& sourcePath, bool createMissingMeta) {
        const std::filesystem::path normalizedSource = NormalizeProjectPath(sourcePath);
        const std::filesystem::path absoluteSource = ResolveProjectPath(projectRoot_, normalizedSource);

        AssetRecord record{};
        record.sourcePath = normalizedSource;
        record.metaPath = GetMetaPathForSource(normalizedSource);
        record.sourceExists = std::filesystem::exists(absoluteSource);
        record.metaExists = std::filesystem::exists(record.metaPath);
        record.type = GuessAssetTypeFromPath(normalizedSource);
        record.displayName = normalizedSource.stem().string();

        if (record.metaExists) {
            AssetMeta meta{};
            if (ReadMeta(record.metaPath, meta)) {
                record.meta = std::move(meta);
                record.guid = record.meta.guid;
                record.type = record.meta.type == AssetType::Unknown ? record.type : record.meta.type;
                record.displayName = record.meta.displayName.empty() ? record.displayName : record.meta.displayName;
                if (record.meta.sourcePath.empty() || NormalizeProjectPath(record.meta.sourcePath) != normalizedSource) {
                    record.meta.sourcePath = normalizedSource.generic_string();
                    if (createMissingMeta) {
                        WriteMeta(record);
                    }
                }
            } else {
                record.meta = AssetMeta{};
                record.meta.sourcePath = normalizedSource.generic_string();
                record.meta.displayName = record.displayName;
                record.meta.type = record.type;
                record.lastImportMessage = "[AssetDatabase] meta JSON parse failed";
            }
        } else if (createMissingMeta) {
            AssetGuid guid = GenerateAssetGuid();
            while (recordsByGuid_.find(guid.value) != recordsByGuid_.end()) {
                guid = GenerateAssetGuid();
            }

            const std::string importerId = SelectDefaultImporterId(record.type, normalizedSource);
            if (const IAssetImporter* importer = importerRegistry_.FindById(importerId)) {
                record.meta = importer->CreateDefaultMeta(normalizedSource, guid);
            } else {
                record.meta = AssetMeta{};
                record.meta.guid = guid;
                record.meta.type = record.type;
                record.meta.importerId = importerId;
                record.meta.sourcePath = normalizedSource.generic_string();
                record.meta.displayName = record.displayName;
                record.meta.importSettingsJson = "{}";
            }
            record.guid = record.meta.guid;
            record.displayName = record.meta.displayName;
            WriteMeta(record);
            record.metaExists = true;
        } else {
            record.meta = AssetMeta{};
            record.meta.type = record.type;
            record.meta.sourcePath = normalizedSource.generic_string();
            record.meta.displayName = record.displayName;
        }

        if (!record.guid.IsValid() && record.meta.guid.IsValid()) {
            record.guid = record.meta.guid;
        }
        if (record.guid.IsValid() && !IsValidAssetGuid(record.guid.value)) {
            record.lastImportMessage = "[AssetDatabase] invalid GUID format: " + record.guid.value;
        }
        if (record.guid.IsValid()) {
            record.importedDirectory = GetImportedDirectory(record.guid);
        }

        RefreshRecordState(record);
        return record;
    }

    AssetType AssetDatabase::GuessAssetTypeFromPath(const std::filesystem::path& sourcePath) const {
        const std::string ext = ToLowerCopy(sourcePath.extension().string());
        const std::string generic = ToLowerCopy(sourcePath.generic_string());

        if (ext == ".dds" && IsSkyFolderPath(sourcePath)) {
            return AssetType::Sky;
        }
        if (IsTextureExtension(ext)) {
            return AssetType::Texture;
        }
        if (IsModelExtension(ext)) {
            return AssetType::Model;
        }
        if (IsScenePath(sourcePath)) {
            return AssetType::Scene;
        }
        if (ext == ".hmat" || EndsWith(generic, ".mat.json")) {
            return AssetType::Material;
        }
        if (IsVfxExtension(ext)) {
            return AssetType::VfxEffect;
        }
        return AssetType::Unknown;
    }

    std::string AssetDatabase::SelectDefaultImporterId(AssetType type, const std::filesystem::path& sourcePath) const {
        if (type == AssetType::Sky) {
            return "SkyCubemapImporter";
        }
        if (type == AssetType::Texture) {
            return "TextureImporter";
        }
        if (type == AssetType::Model) {
            return "ModelImporterStub";
        }
        if (type == AssetType::Scene) {
            return "SceneImporterStub";
        }
        if (type == AssetType::Material) {
            return "MaterialImporterStub";
        }
        if (type == AssetType::VfxEffect) {
            return "VfxImporterStub";
        }

        if (const IAssetImporter* importer = importerRegistry_.FindForSource(sourcePath)) {
            return importer->GetImporterId();
        }
        return {};
    }

    void AssetDatabase::RegisterDefaultImporters() {
        importerRegistry_.Register(std::make_unique<SkyCubemapImporter>(
            std::make_unique<DirectXTexTextureImportBackend>()));
        importerRegistry_.Register(std::make_unique<TextureImporter>(
            std::make_unique<DirectXTexTextureImportBackend>()));
        importerRegistry_.Register(std::make_unique<ModelImporterStub>());
        importerRegistry_.Register(std::make_unique<SceneImporterStub>());
        importerRegistry_.Register(std::make_unique<MaterialImporterStub>());
        importerRegistry_.Register(std::make_unique<VfxImporterStub>());
    }

    void AssetDatabase::EnsureProjectDirectories() const {
        std::error_code ec{};
        const std::filesystem::path roots[] = {
            assetsRoot_,
            assetsRoot_ / "Models",
            assetsRoot_ / "Textures",
            assetsRoot_ / "Materials",
            assetsRoot_ / "Skies",
            assetsRoot_ / "IBL",
            assetsRoot_ / "Vfx",
            assetsRoot_ / "Shaders",
            assetsRoot_ / "Scenes",
            libraryRoot_,
            libraryRoot_ / "Imported",
            libraryRoot_ / "Thumbnails",
            libraryRoot_ / "AssetDatabase",
            libraryRoot_ / "ShaderCache",
            projectSettingsRoot_,
        };

        for (const std::filesystem::path& root : roots) {
            std::filesystem::create_directories(root, ec);
            ec.clear();
        }

        const std::filesystem::path settingsPath = projectSettingsRoot_ / "asset_pipeline.json";
        if (!std::filesystem::exists(settingsPath, ec)) {
            nlohmann::json settings{
                { "version", 1 },
                { "assetsRoot", "Assets" },
                { "libraryRoot", "Library" },
                { "importedRoot", "Library/Imported" },
                { "thumbnailRoot", "Library/Thumbnails" },
                { "assetDatabaseRoot", "Library/AssetDatabase" },
                { "shaderCacheRoot", "Library/ShaderCache" },
            };

            std::ofstream ofs(settingsPath);
            if (ofs.is_open()) {
                ofs << settings.dump(2) << '\n';
            }
        }
    }

    void AssetDatabase::AddDirectoryToCache(const std::filesystem::path& directory) {
        if (directory.empty()) {
            return;
        }

        std::filesystem::path normalized = NormalizeProjectPath(directory).lexically_normal();
        if (normalized.empty()) {
            return;
        }

        directories_.push_back(std::move(normalized));
    }

    void AssetDatabase::SortAndUniqueDirectories() {
        std::sort(directories_.begin(), directories_.end(), [this](const auto& lhs, const auto& rhs) {
            return MakePathKey(lhs) < MakePathKey(rhs);
        });
        directories_.erase(std::unique(directories_.begin(), directories_.end(), [this](const auto& lhs, const auto& rhs) {
            return MakePathKey(lhs) == MakePathKey(rhs);
        }), directories_.end());
    }

    void AssetDatabase::RefreshRecordState(AssetRecord& record) const {
        const std::filesystem::path absoluteSource = ResolveProjectPath(projectRoot_, record.sourcePath);
        record.sourceExists = std::filesystem::exists(absoluteSource);
        record.metaExists = std::filesystem::exists(record.metaPath);
        record.importerMissing = !record.meta.importerId.empty() &&
            importerRegistry_.FindById(record.meta.importerId) == nullptr;

        if (record.guid.IsValid()) {
            record.importedDirectory = GetImportedDirectory(record.guid);
        }

        bool artifactMissing = false;
        std::filesystem::file_time_type oldestArtifactTime{};
        bool hasArtifactTime = false;
        for (const AssetArtifactDesc& artifact : record.meta.artifacts) {
            const std::filesystem::path artifactPath = ResolveProjectPath(projectRoot_, artifact.path);
            if (!std::filesystem::exists(artifactPath)) {
                artifactMissing = true;
                continue;
            }

            std::error_code ec{};
            const auto writeTime = std::filesystem::last_write_time(artifactPath, ec);
            if (!ec && (!hasArtifactTime || writeTime < oldestArtifactTime)) {
                oldestArtifactTime = writeTime;
                hasArtifactTime = true;
            }
        }
        record.artifactMissing = artifactMissing;

        bool sourceNewerThanArtifact = false;
        if (record.sourceExists && hasArtifactTime) {
            std::error_code ec{};
            const auto sourceTime = std::filesystem::last_write_time(absoluteSource, ec);
            sourceNewerThanArtifact = !ec && sourceTime > oldestArtifactTime;
        }

        const IAssetImporter* importer = importerRegistry_.FindById(record.meta.importerId);
        const bool importerVersionOutdated = importer && record.meta.importerVersion != importer->GetImporterVersion();
        const bool textureNeedsArtifact = record.type == AssetType::Texture && record.meta.artifacts.empty();
        const bool modelNeedsArtifact = record.type == AssetType::Model && record.meta.artifacts.empty();
        const bool skyNeedsArtifact = record.type == AssetType::Sky && record.meta.artifacts.empty();
        record.importOutdated = record.importerMissing ||
            record.artifactMissing ||
            sourceNewerThanArtifact ||
            importerVersionOutdated ||
            textureNeedsArtifact ||
            modelNeedsArtifact ||
            skyNeedsArtifact;

        const std::filesystem::path reportPath = record.importedDirectory / "import_report.json";
        nlohmann::json report;
        if (!record.importedDirectory.empty() && ReadJsonFile(reportPath, report)) {
            record.lastImportSucceeded = report.value("success", false);
            record.lastImportMessage = report.value("message", record.lastImportMessage);
        }
    }

    bool AssetDatabase::WriteImportReport(const AssetRecord& record, const AssetImportResult& result) const {
        if (record.importedDirectory.empty()) {
            return false;
        }

        std::error_code ec{};
        std::filesystem::create_directories(record.importedDirectory, ec);
        if (ec) {
            HIKARI_LOG_ERROR("[AssetDatabase] failed to create import report directory: " + record.importedDirectory.generic_string());
            return false;
        }

        nlohmann::json root{
            { "guid", record.guid.value },
            { "sourcePath", record.sourcePath.generic_string() },
            { "importerId", record.meta.importerId },
            { "importerVersion", record.meta.importerVersion },
            { "success", result.success },
            { "message", result.message },
            { "artifacts", SerializeArtifacts(result.success ? result.artifacts : record.meta.artifacts) },
            { "dependencies", SerializeDependencies(result.success ? result.dependencies : record.meta.dependencies) },
        };

        if (!result.diagnosticsJson.empty()) {
            nlohmann::json diagnostics = nlohmann::json::parse(result.diagnosticsJson, nullptr, false);
            if (diagnostics.is_discarded()) {
                root["diagnosticsText"] = result.diagnosticsJson;
            } else {
                root["diagnostics"] = std::move(diagnostics);
            }
        }

        const std::filesystem::path reportPath = record.importedDirectory / "import_report.json";
        std::ofstream ofs(reportPath);
        if (!ofs.is_open()) {
            HIKARI_LOG_ERROR("[AssetDatabase] failed to write import report: " + reportPath.generic_string());
            return false;
        }
        ofs << root.dump(2) << '\n';
        return true;
    }

    std::filesystem::path AssetDatabase::NormalizeProjectPath(const std::filesystem::path& path) const {
        if (path.empty()) {
            return {};
        }

        std::filesystem::path normalized = path.lexically_normal();
        if (normalized.is_absolute() && !projectRoot_.empty()) {
            std::error_code ec{};
            const std::filesystem::path relative = std::filesystem::relative(normalized, projectRoot_, ec);
            if (!ec && !relative.empty() && relative.native().find(L"..") != 0) {
                normalized = relative;
            }
        }
        return normalized.lexically_normal();
    }

    std::string AssetDatabase::MakePathKey(const std::filesystem::path& path) const {
        return ToLowerCopy(NormalizeProjectPath(path).generic_string());
    }

    bool AssetDatabase::IsPathUnderDirectory(
        const std::filesystem::path& path,
        const std::filesystem::path& directory) const {

        const std::string pathKey = MakePathKey(path);
        const std::string directoryKey = MakePathKey(directory);
        if (directoryKey.empty()) {
            return false;
        }
        if (pathKey == directoryKey) {
            return true;
        }
        const std::string prefix = directoryKey.back() == '/'
            ? directoryKey
            : directoryKey + '/';
        return pathKey.rfind(prefix, 0) == 0;
    }

} // namespace HIKARI
