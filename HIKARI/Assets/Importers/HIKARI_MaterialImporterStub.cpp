#include "HIKARI_MaterialImporterStub.h"

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

        bool EndsWith(std::string_view text, std::string_view suffix) {
            return text.size() >= suffix.size() &&
                text.substr(text.size() - suffix.size()) == suffix;
        }
    }

    const char* MaterialImporterStub::GetImporterId() const {
        return "MaterialImporterStub";
    }

    uint32_t MaterialImporterStub::GetImporterVersion() const {
        return 1;
    }

    bool MaterialImporterStub::CanImport(const std::filesystem::path& sourcePath) const {
        const std::string path = ToLowerCopy(sourcePath.generic_string());
        return sourcePath.extension() == ".hmat" || EndsWith(path, ".mat.json");
    }

    AssetMeta MaterialImporterStub::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {

        AssetMeta meta{};
        meta.metaVersion = 1;
        meta.guid = guid;
        meta.type = AssetType::Material;
        meta.importerId = GetImporterId();
        meta.importerVersion = GetImporterVersion();
        meta.sourcePath = sourcePath.generic_string();
        meta.displayName = sourcePath.stem().string();
        meta.importSettingsJson = nlohmann::json{
            { "sourceFormat", sourcePath.extension().string() },
            { "futureOutputFormat", "HMAT" },
            { "cookMaterial", false },
        }.dump(2);
        return meta;
    }

    AssetImportResult MaterialImporterStub::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {

        (void)context;
        AssetImportResult result{};
        result.success = true;
        result.message = "[AssetImporter] Material cook not implemented yet. source=" +
            record.sourcePath.generic_string();
        return result;
    }

} // namespace HIKARI
