#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "Assets/HIKARI_AssetArtifactManifest.h"
#include "Assets/HIKARI_AssetMeta.h"
#include "Assets/HIKARI_AssetRecord.h"

namespace HIKARI {

    class AssetTaskContext;

    struct AssetImportContext {
        std::filesystem::path projectRoot{};
        std::filesystem::path assetsRoot{};
        std::filesystem::path libraryRoot{};
        std::filesystem::path sourceMetaRoot{};
        std::filesystem::path importedDirectory{};
        AssetTaskContext* task = nullptr;
    };

    struct AssetImportResult {
        bool success = false;
        std::string message{};
        std::string diagnosticsJson{};
        std::vector<AssetArtifactDesc> artifacts{};
        std::vector<AssetDependencyDesc> dependencies{};
    };

    class IAssetImporter {
    public:
        virtual ~IAssetImporter() = default;

        virtual const char* GetImporterId() const = 0;
        virtual uint32_t GetImporterVersion() const = 0;
        virtual bool CanImport(const std::filesystem::path& sourcePath) const = 0;

        virtual AssetMeta CreateDefaultMeta(
            const std::filesystem::path& sourcePath,
            const AssetGuid& guid) const = 0;

        virtual AssetImportResult Import(
            const AssetRecord& record,
            const AssetImportContext& context) = 0;
    };

} // namespace HIKARI
