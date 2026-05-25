#include "HIKARI_LegacyAssetJsonMigrator.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

#include <json.hpp>

#include "Assets/HIKARI_AssetDatabase.h"
#include "Core/HIKARI_Logger.h"

namespace HIKARI {

    namespace {
        struct PendingLegacyAsset {
            std::string oldId{};
            std::filesystem::path sourcePath{};
            std::string destinationFolder{};
            std::string message{};
        };

        struct PendingLegacySky {
            std::string oldId{};
            std::string textureAssetId{};
            std::filesystem::path sourcePath{};
            std::string message{};
        };

        std::string ToLowerCopy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        bool StartsWith(std::string_view text, std::string_view prefix) {
            return text.size() >= prefix.size() && text.substr(0, prefix.size()) == prefix;
        }

        bool IsBuiltinPath(const std::filesystem::path& path) {
            return StartsWith(ToLowerCopy(path.generic_string()), "builtin:");
        }

        bool IsInsideProjectPath(const std::filesystem::path& relativePath) {
            const std::string key = relativePath.generic_string();
            return key != ".." && !StartsWith(key, "../") && !StartsWith(key, "..\\");
        }

        std::filesystem::path NormalizeProjectPath(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& path) {

            if (path.empty()) {
                return {};
            }
            if (IsBuiltinPath(path)) {
                return path;
            }

            std::error_code ec{};
            std::filesystem::path normalized = path.lexically_normal();
            if (normalized.is_absolute()) {
                const std::filesystem::path relative = std::filesystem::relative(normalized, projectRoot, ec);
                if (!ec && IsInsideProjectPath(relative)) {
                    normalized = relative;
                }
            }
            return normalized.lexically_normal();
        }

        std::filesystem::path AbsoluteProjectPath(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& path) {

            if (path.empty() || IsBuiltinPath(path)) {
                return {};
            }
            if (path.is_absolute()) {
                return path.lexically_normal();
            }
            return (projectRoot / path).lexically_normal();
        }

        bool IsAssetsPath(const std::filesystem::path& path) {
            const std::string key = ToLowerCopy(path.generic_string());
            return key == "assets" || StartsWith(key, "assets/");
        }

        bool ReadJson(const std::filesystem::path& path, nlohmann::json& outRoot) {
            std::ifstream ifs(path);
            if (!ifs.is_open()) {
                return false;
            }

            outRoot = nlohmann::json::parse(ifs, nullptr, false);
            return !outRoot.is_discarded() && outRoot.is_object();
        }

        bool WriteJson(const std::filesystem::path& path, const nlohmann::json& root) {
            std::error_code ec{};
            std::filesystem::create_directories(path.parent_path(), ec);
            if (ec) {
                return false;
            }

            std::ofstream ofs(path);
            if (!ofs.is_open()) {
                return false;
            }
            ofs << root.dump(2) << '\n';
            return true;
        }

        void LoadLegacyAssets(
            const std::filesystem::path& projectRoot,
            const char* fileName,
            const char* arrayName,
            const char* destinationFolder,
            std::vector<PendingLegacyAsset>& out) {

            nlohmann::json root;
            if (!ReadJson(projectRoot / "Data" / fileName, root) || !root.contains(arrayName) || !root[arrayName].is_array()) {
                return;
            }

            for (const nlohmann::json& node : root[arrayName]) {
                if (!node.is_object()) {
                    continue;
                }

                PendingLegacyAsset asset{};
                asset.oldId = node.value("id", "");
                asset.sourcePath = NormalizeProjectPath(projectRoot, node.value("sourcePath", ""));
                asset.destinationFolder = destinationFolder;
                if (!asset.oldId.empty()) {
                    out.push_back(std::move(asset));
                }
            }
        }

        void LoadLegacySkies(
            const std::filesystem::path& projectRoot,
            std::vector<PendingLegacySky>& out) {

            nlohmann::json root;
            if (!ReadJson(projectRoot / "Data" / "assets_skies.json", root) ||
                !root.contains("skies") ||
                !root["skies"].is_array()) {
                return;
            }

            for (const nlohmann::json& node : root["skies"]) {
                if (!node.is_object()) {
                    continue;
                }

                PendingLegacySky sky{};
                sky.oldId = node.value("id", "");
                sky.textureAssetId = node.value("textureAssetId", "");
                sky.sourcePath = NormalizeProjectPath(projectRoot, node.value("sourcePath", ""));
                if (!sky.oldId.empty()) {
                    out.push_back(std::move(sky));
                }
            }
        }

        void PrepareSourceForAssetDatabase(
            const std::filesystem::path& projectRoot,
            const LegacyAssetMigrationOptions& options,
            PendingLegacyAsset& asset) {

            if (asset.sourcePath.empty()) {
                asset.message = "missing sourcePath";
                return;
            }
            if (IsBuiltinPath(asset.sourcePath)) {
                asset.message = "builtin asset cannot be migrated automatically";
                return;
            }
            if (IsAssetsPath(asset.sourcePath)) {
                return;
            }

            if (!options.copyExternalSourcesToAssets) {
                asset.message = "source is outside Assets";
                return;
            }

            const std::filesystem::path absoluteSource = AbsoluteProjectPath(projectRoot, asset.sourcePath);
            std::error_code ec{};
            if (!std::filesystem::exists(absoluteSource, ec)) {
                asset.message = "source missing";
                return;
            }

            const std::filesystem::path destination =
                projectRoot / "Assets" / asset.destinationFolder / absoluteSource.filename();
            std::filesystem::create_directories(destination.parent_path(), ec);
            if (ec) {
                asset.message = "failed to create destination folder";
                return;
            }

            if (!std::filesystem::exists(destination, ec)) {
                std::filesystem::copy_file(absoluteSource, destination, std::filesystem::copy_options::none, ec);
                if (ec) {
                    asset.message = "failed to copy source into Assets";
                    return;
                }
            }

            const std::filesystem::path relativeDestination =
                std::filesystem::relative(destination, projectRoot, ec);
            asset.sourcePath = ec ? destination.lexically_normal() : relativeDestination.lexically_normal();
            asset.message = "copied source into Assets";
        }

        LegacyAssetMigrationEntry ResolvePendingAsset(
            const PendingLegacyAsset& pending,
            const AssetDatabase& assetDatabase,
            std::unordered_map<std::string, std::string>& oldToGuid,
            std::unordered_map<std::string, std::filesystem::path>& oldToSourcePath) {

            LegacyAssetMigrationEntry entry{};
            entry.oldId = pending.oldId;
            entry.sourcePath = pending.sourcePath;
            entry.message = pending.message;

            if (!pending.sourcePath.empty() && !IsBuiltinPath(pending.sourcePath)) {
                if (const AssetRecord* record = assetDatabase.FindByPath(pending.sourcePath)) {
                    entry.newGuid = record->guid.value;
                    entry.sourcePath = record->sourcePath;
                    oldToGuid[pending.oldId] = entry.newGuid;
                    oldToSourcePath[pending.oldId] = entry.sourcePath;
                    if (entry.message.empty()) {
                        entry.message = "mapped";
                    }
                } else if (entry.message.empty()) {
                    entry.message = "source was not found by AssetDatabase";
                }
            }

            return entry;
        }

        LegacyAssetMigrationEntry ResolvePendingSky(
            const PendingLegacySky& pending,
            const AssetDatabase& assetDatabase,
            const std::unordered_map<std::string, std::string>& oldToGuid,
            const std::unordered_map<std::string, std::filesystem::path>& oldToSourcePath,
            std::unordered_map<std::string, std::string>& mutableOldToGuid) {

            LegacyAssetMigrationEntry entry{};
            entry.oldId = pending.oldId;
            entry.sourcePath = pending.sourcePath;
            entry.message = pending.message;

            if (!pending.sourcePath.empty()) {
                if (const AssetRecord* record = assetDatabase.FindByPath(pending.sourcePath)) {
                    entry.newGuid = record->guid.value;
                    entry.sourcePath = record->sourcePath;
                    entry.message = "mapped direct sky source";
                }
            } else if (!pending.textureAssetId.empty()) {
                const auto guidIt = oldToGuid.find(pending.textureAssetId);
                if (guidIt != oldToGuid.end()) {
                    entry.newGuid = guidIt->second;
                    const auto sourceIt = oldToSourcePath.find(pending.textureAssetId);
                    if (sourceIt != oldToSourcePath.end()) {
                        entry.sourcePath = sourceIt->second;
                    }
                    entry.message = "mapped through legacy textureAssetId";
                }
            }

            if (!entry.newGuid.empty()) {
                mutableOldToGuid[pending.oldId] = entry.newGuid;
            } else if (entry.message.empty()) {
                entry.message = "legacy sky textureAssetId could not be resolved";
            }

            return entry;
        }

        nlohmann::json SerializeEntries(const std::vector<LegacyAssetMigrationEntry>& entries) {
            nlohmann::json out = nlohmann::json::array();
            for (const LegacyAssetMigrationEntry& entry : entries) {
                out.push_back({
                    { "oldId", entry.oldId },
                    { "newGuid", entry.newGuid },
                    { "sourcePath", entry.sourcePath.generic_string() },
                    { "message", entry.message },
                });
            }
            return out;
        }

        nlohmann::json SerializeSceneRewrites(const std::vector<LegacySceneRewriteEntry>& entries) {
            nlohmann::json out = nlohmann::json::array();
            for (const LegacySceneRewriteEntry& entry : entries) {
                out.push_back({
                    { "scenePath", entry.scenePath.generic_string() },
                    { "field", entry.field },
                    { "oldId", entry.oldId },
                    { "newGuid", entry.newGuid },
                });
            }
            return out;
        }

        bool IsSceneAssetReferenceField(const std::string& key) {
            return key == "assetId" ||
                key == "skyAsset" ||
                key == "effectAssetId" ||
                key == "textureAssetId" ||
                key == "meshAssetId";
        }

        bool RewriteSceneJson(
            nlohmann::json& node,
            const std::filesystem::path& scenePath,
            const std::unordered_map<std::string, std::string>& oldToGuid,
            std::vector<LegacySceneRewriteEntry>& rewrites) {

            bool changed = false;
            if (node.is_object()) {
                for (auto& item : node.items()) {
                    nlohmann::json& value = item.value();
                    if (IsSceneAssetReferenceField(item.key()) && value.is_string()) {
                        const std::string oldId = value.get<std::string>();
                        const auto it = oldToGuid.find(oldId);
                        if (it != oldToGuid.end()) {
                            value = it->second;
                            rewrites.push_back(LegacySceneRewriteEntry{
                                scenePath,
                                item.key(),
                                oldId,
                                it->second
                            });
                            changed = true;
                            continue;
                        }
                    }
                    changed = RewriteSceneJson(value, scenePath, oldToGuid, rewrites) || changed;
                }
            } else if (node.is_array()) {
                for (nlohmann::json& child : node) {
                    changed = RewriteSceneJson(child, scenePath, oldToGuid, rewrites) || changed;
                }
            }
            return changed;
        }

        void RewriteScenes(
            const std::filesystem::path& projectRoot,
            const std::unordered_map<std::string, std::string>& oldToGuid,
            std::vector<LegacySceneRewriteEntry>& rewrites) {

            const std::filesystem::path scenesRoot = projectRoot / "Data" / "scenes";
            std::error_code ec{};
            if (!std::filesystem::exists(scenesRoot, ec)) {
                return;
            }

            for (const std::filesystem::directory_entry& entry :
                std::filesystem::directory_iterator(scenesRoot, ec)) {
                if (ec || !entry.is_regular_file() || entry.path().extension() != ".json") {
                    continue;
                }

                nlohmann::json root;
                if (!ReadJson(entry.path(), root)) {
                    continue;
                }

                const std::filesystem::path relativeScenePath =
                    NormalizeProjectPath(projectRoot, entry.path());
                std::vector<LegacySceneRewriteEntry> localRewrites;
                if (RewriteSceneJson(root, relativeScenePath, oldToGuid, localRewrites) &&
                    WriteJson(entry.path(), root)) {
                    rewrites.insert(rewrites.end(), localRewrites.begin(), localRewrites.end());
                }
            }
        }
    }

    LegacyAssetMigrationResult LegacyAssetJsonMigrator::Migrate(
        AssetDatabase& assetDatabase,
        const LegacyAssetMigrationOptions& options) const {

        if (assetDatabase.GetProjectRoot().empty()) {
            assetDatabase.Initialize(std::filesystem::current_path());
        }

        const std::filesystem::path projectRoot = assetDatabase.GetProjectRoot();
        LegacyAssetMigrationResult result{};
        result.reportPath = projectRoot / "Library" / "AssetDatabase" / "legacy_asset_migration_report.json";

        std::vector<PendingLegacyAsset> models;
        std::vector<PendingLegacyAsset> textures;
        std::vector<PendingLegacyAsset> vfx;
        std::vector<PendingLegacySky> skies;

        LoadLegacyAssets(projectRoot, "assets_models.json", "models", "Models", models);
        LoadLegacyAssets(projectRoot, "assets_textures.json", "textures", "Textures", textures);
        LoadLegacyAssets(projectRoot, "assets_vfx.json", "vfx", "Vfx", vfx);
        LoadLegacySkies(projectRoot, skies);

        for (PendingLegacyAsset& asset : models) {
            PrepareSourceForAssetDatabase(projectRoot, options, asset);
        }
        for (PendingLegacyAsset& asset : textures) {
            PrepareSourceForAssetDatabase(projectRoot, options, asset);
        }
        for (PendingLegacyAsset& asset : vfx) {
            PrepareSourceForAssetDatabase(projectRoot, options, asset);
        }

        assetDatabase.ScanAssets(true);

        std::unordered_map<std::string, std::string> oldToGuid;
        std::unordered_map<std::string, std::filesystem::path> oldToSourcePath;

        for (const PendingLegacyAsset& pending : models) {
            result.models.push_back(ResolvePendingAsset(pending, assetDatabase, oldToGuid, oldToSourcePath));
        }
        for (const PendingLegacyAsset& pending : textures) {
            result.textures.push_back(ResolvePendingAsset(pending, assetDatabase, oldToGuid, oldToSourcePath));
        }
        for (const PendingLegacyAsset& pending : vfx) {
            result.vfx.push_back(ResolvePendingAsset(pending, assetDatabase, oldToGuid, oldToSourcePath));
        }
        for (const PendingLegacySky& pending : skies) {
            result.skies.push_back(ResolvePendingSky(pending, assetDatabase, oldToGuid, oldToSourcePath, oldToGuid));
        }

        if (options.rewriteScenes) {
            RewriteScenes(projectRoot, oldToGuid, result.sceneRewrites);
        }

        nlohmann::json report{
            { "models", SerializeEntries(result.models) },
            { "skies", SerializeEntries(result.skies) },
            { "textures", SerializeEntries(result.textures) },
            { "vfx", SerializeEntries(result.vfx) },
            { "sceneRewrites", SerializeSceneRewrites(result.sceneRewrites) },
        };

        result.success = WriteJson(result.reportPath, report);
        result.message = result.success
            ? "[AssetDatabase] legacy asset migration report written"
            : "[AssetDatabase] failed to write legacy asset migration report";

        if (result.success) {
            HIKARI_LOG_INFO(result.message + ": " + result.reportPath.generic_string());
        } else {
            HIKARI_LOG_ERROR(result.message + ": " + result.reportPath.generic_string());
        }
        return result;
    }

} // namespace HIKARI
