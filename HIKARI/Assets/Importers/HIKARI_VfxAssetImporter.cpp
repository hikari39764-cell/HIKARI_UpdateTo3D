#include "HIKARI_VfxAssetImporter.h"

#include <algorithm>
#include <cctype>

#include <json.hpp>

namespace HIKARI {

    namespace {
        std::string ToLowerCopy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }
    }

    const char* VfxAssetImporter::GetImporterId() const {
        return "VfxAssetImporter";
    }

    uint32_t VfxAssetImporter::GetImporterVersion() const {
        return 1;
    }

    bool VfxAssetImporter::CanImport(const std::filesystem::path& sourcePath) const {
        const std::string ext = ToLowerCopy(sourcePath.extension().string());
        return ext == ".efk" || ext == ".efkefc";
    }

    AssetMeta VfxAssetImporter::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {

        AssetMeta meta{};
        meta.metaVersion = 1;
        meta.guid = guid;
        meta.type = AssetType::VfxEffect;
        meta.importerId = GetImporterId();
        meta.importerVersion = GetImporterVersion();
        meta.sourcePath = sourcePath.generic_string();
        meta.displayName = sourcePath.stem().string();
        meta.importSettingsJson = nlohmann::json{
            { "sourceFormat", sourcePath.extension().string() },
            { "futureOutputFormat", "HPAK" },
            { "cookVfx", false },
        }.dump(2);
        return meta;
    }

    AssetImportResult VfxAssetImporter::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {

        (void)context;
        AssetImportResult result{};
        result.success = true;
        result.message = "[AssetImporter] VFX cook not implemented yet; existing Effekseer asset path remains authoritative. source=" +
            record.sourcePath.generic_string();
        return result;
    }

} // namespace HIKARI
