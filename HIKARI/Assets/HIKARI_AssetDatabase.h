#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "HIKARI_AssetRecord.h"
#include "Importers/HIKARI_AssetImporterRegistry.h"

namespace HIKARI {

    struct AssetImportBatchResult {
        int attempted = 0;
        int succeeded = 0;
        int failed = 0;
    };

    class AssetDatabase {
    public:
        bool Initialize(const std::filesystem::path& projectRoot);

        const std::filesystem::path& GetProjectRoot() const;
        const std::filesystem::path& GetAssetsRoot() const;
        const std::filesystem::path& GetLibraryRoot() const;

        AssetImporterRegistry& GetImporterRegistry();
        const AssetImporterRegistry& GetImporterRegistry() const;

        bool ScanAssets(bool createMissingMeta);
        bool ImportAsset(const AssetGuid& guid);
        AssetImportBatchResult ImportAllOutdated();
        AssetImportBatchResult ImportDependencies(const AssetGuid& guid, bool includeSelf = false);

        const AssetRecord* FindByGuid(const AssetGuid& guid) const;
        AssetRecord* FindByGuid(const AssetGuid& guid);

        const AssetRecord* FindByPath(const std::filesystem::path& path) const;
        AssetRecord* FindByPath(const std::filesystem::path& path);

        std::vector<const AssetRecord*> CollectByType(AssetType type) const;
        std::vector<const AssetRecord*> CollectAll() const;
        std::vector<std::filesystem::path> CollectDirectories() const;
        std::vector<const AssetRecord*> CollectInDirectory(const std::filesystem::path& directory, bool recursive) const;

        bool WriteMeta(const AssetRecord& record);
        bool ReadMeta(const std::filesystem::path& metaPath, AssetMeta& outMeta) const;
        bool RegenerateMeta(const std::filesystem::path& sourcePath);

        std::filesystem::path GetMetaPathForSource(const std::filesystem::path& sourcePath) const;
        std::filesystem::path GetImportedDirectory(const AssetGuid& guid) const;

    private:
        AssetRecord BuildRecordForSource(const std::filesystem::path& sourcePath, bool createMissingMeta);
        AssetType GuessAssetTypeFromPath(const std::filesystem::path& sourcePath) const;
        std::string SelectDefaultImporterId(AssetType type, const std::filesystem::path& sourcePath) const;

        void RegisterDefaultImporters();
        void EnsureProjectDirectories() const;
        void AddDirectoryToCache(const std::filesystem::path& directory);
        void SortAndUniqueDirectories();
        void RefreshRecordState(AssetRecord& record) const;
        bool WriteImportReport(const AssetRecord& record, const AssetImportResult& result) const;

        std::filesystem::path NormalizeProjectPath(const std::filesystem::path& path) const;
        std::string MakePathKey(const std::filesystem::path& path) const;
        bool IsPathUnderDirectory(const std::filesystem::path& path, const std::filesystem::path& directory) const;

        std::filesystem::path projectRoot_{};
        std::filesystem::path assetsRoot_{};
        std::filesystem::path libraryRoot_{};
        std::filesystem::path projectSettingsRoot_{};

        AssetImporterRegistry importerRegistry_{};
        std::vector<AssetRecord> records_{};
        std::vector<std::filesystem::path> directories_{};
        std::unordered_map<std::string, size_t> recordsByGuid_{};
        std::unordered_map<std::string, size_t> guidByNormalizedPath_{};
    };

} // namespace HIKARI
