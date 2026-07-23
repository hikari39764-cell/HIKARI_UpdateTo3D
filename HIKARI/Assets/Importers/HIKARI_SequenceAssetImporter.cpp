#include "Assets/Importers/HIKARI_SequenceAssetImporter.h"

#include <algorithm>
#include <cctype>

#include <json.hpp>

#include "Assets/Sequence/HIKARI_SequenceAsset.h"
#include "Core/Text/HIKARI_AsciiCase.h"

namespace HIKARI {

    const char* SequenceAssetImporter::GetImporterId() const {
        return "SequenceAssetImporter";
    }

    uint32_t SequenceAssetImporter::GetImporterVersion() const {
        return 1;
    }

    bool SequenceAssetImporter::CanImport(
        const std::filesystem::path& sourcePath) const {

        return TEXT::ToLowerAsciiCopy(sourcePath.extension().string()) ==
            ".hsequence";
    }

    AssetMeta SequenceAssetImporter::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {

        AssetMeta meta{};
        meta.guid = guid;
        meta.type = AssetType::Sequence;
        meta.importerId = GetImporterId();
        meta.importerVersion = GetImporterVersion();
        meta.sourcePath = sourcePath.generic_string();
        meta.displayName = sourcePath.stem().string();
        meta.importSettingsJson = nlohmann::json{
            { "runtimeLoader", "SequenceAssetStore" },
            { "sourceAuthoritative", true }
        }.dump(2);
        return meta;
    }

    AssetImportResult SequenceAssetImporter::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {

        AssetImportResult result{};
        SequenceAsset asset{};
        std::string error{};
        const std::filesystem::path sourcePath = record.sourcePath.is_absolute()
            ? record.sourcePath
            : (context.projectRoot / record.sourcePath).lexically_normal();
        result.success = LoadSequenceAsset(
            sourcePath,
            record.guid,
            asset,
            &error);
        result.message = result.success
            ? "Sequence asset validated"
            : "Sequence asset validation failed: " + error;
        result.diagnosticsJson = nlohmann::json{
            { "kind", "Sequence" },
            { "durationSeconds", asset.sequence.durationSeconds },
            { "bindingCount", asset.sequence.bindings.size() },
            { "valid", result.success }
        }.dump(2);
        return result;
    }

} // namespace HIKARI
