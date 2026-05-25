#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace HIKARI {

    class AssetDatabase;

    struct LegacyAssetMigrationOptions {
        bool rewriteScenes = false;
        bool copyExternalSourcesToAssets = true;
    };

    struct LegacyAssetMigrationEntry {
        std::string oldId{};
        std::string newGuid{};
        std::filesystem::path sourcePath{};
        std::string message{};
    };

    struct LegacySceneRewriteEntry {
        std::filesystem::path scenePath{};
        std::string field{};
        std::string oldId{};
        std::string newGuid{};
    };

    struct LegacyAssetMigrationResult {
        bool success = false;
        std::string message{};
        std::filesystem::path reportPath{};

        std::vector<LegacyAssetMigrationEntry> models{};
        std::vector<LegacyAssetMigrationEntry> skies{};
        std::vector<LegacyAssetMigrationEntry> textures{};
        std::vector<LegacyAssetMigrationEntry> vfx{};
        std::vector<LegacySceneRewriteEntry> sceneRewrites{};
    };

    class LegacyAssetJsonMigrator {
    public:
        LegacyAssetMigrationResult Migrate(
            AssetDatabase& assetDatabase,
            const LegacyAssetMigrationOptions& options = {}) const;
    };

} // namespace HIKARI
