#pragma once

#include "HIKARI_AssetImporter.h"

namespace HIKARI {

    class SequenceAssetImporter final : public IAssetImporter {
    public:
        const char* GetImporterId() const override;
        uint32_t GetImporterVersion() const override;
        bool CanImport(
            const std::filesystem::path& sourcePath) const override;
        AssetMeta CreateDefaultMeta(
            const std::filesystem::path& sourcePath,
            const AssetGuid& guid) const override;
        AssetImportResult Import(
            const AssetRecord& record,
            const AssetImportContext& context) override;
    };

} // namespace HIKARI
