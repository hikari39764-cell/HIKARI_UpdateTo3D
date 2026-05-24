#pragma once

#include <memory>

#include "HIKARI_AssetImporter.h"
#include "HIKARI_TextureImportBackend.h"

namespace HIKARI {

    class TextureImporter final : public IAssetImporter {
    public:
        explicit TextureImporter(std::unique_ptr<ITextureImportBackend> backend);

        const char* GetImporterId() const override;
        uint32_t GetImporterVersion() const override;
        bool CanImport(const std::filesystem::path& sourcePath) const override;

        AssetMeta CreateDefaultMeta(
            const std::filesystem::path& sourcePath,
            const AssetGuid& guid) const override;

        AssetImportResult Import(
            const AssetRecord& record,
            const AssetImportContext& context) override;

    private:
        std::unique_ptr<ITextureImportBackend> backend_{};
    };

} // namespace HIKARI
