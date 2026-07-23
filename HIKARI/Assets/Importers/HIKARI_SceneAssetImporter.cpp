#include "HIKARI_SceneAssetImporter.h"
#include "Core/Text/HIKARI_AsciiCase.h"
#include "Core/Text/HIKARI_AsciiCase.h"

#include <algorithm>
#include <cctype>

#include <json.hpp>

namespace HIKARI {

    namespace {

        bool EndsWith(std::string_view text, std::string_view suffix) {
            return text.size() >= suffix.size() &&
                text.substr(text.size() - suffix.size()) == suffix;
        }

        bool IsSceneSourcePath(const std::filesystem::path& sourcePath) {
            const std::string lowerPath = TEXT::ToLowerAsciiCopy(sourcePath.generic_string());
            const std::string filename = TEXT::ToLowerAsciiCopy(sourcePath.filename().string());
            const std::string ext = TEXT::ToLowerAsciiCopy(sourcePath.extension().string());
            return ext == ".hscene" ||
                EndsWith(filename, ".scene.json") ||
                (ext == ".json" && lowerPath.find("assets/scenes/") != std::string::npos);
        }
    }

    const char* SceneAssetImporter::GetImporterId() const {
        return "SceneAssetImporter";
    }

    uint32_t SceneAssetImporter::GetImporterVersion() const {
        return 1;
    }

    bool SceneAssetImporter::CanImport(const std::filesystem::path& sourcePath) const {
        return IsSceneSourcePath(sourcePath);
    }

    AssetMeta SceneAssetImporter::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {

        AssetMeta meta{};
        meta.metaVersion = 1;
        meta.guid = guid;
        meta.type = AssetType::Scene;
        meta.importerId = GetImporterId();
        meta.importerVersion = GetImporterVersion();
        meta.sourcePath = sourcePath.generic_string();
        meta.displayName = sourcePath.stem().string();
        meta.importSettingsJson = nlohmann::json{
            { "sourceFormat", sourcePath.extension().string() },
            { "runtimeLoader", "SceneSerializer" },
            { "cookScene", false },
            { "outputFormat", "HSCENE" },
        }.dump(2);
        return meta;
    }

    AssetImportResult SceneAssetImporter::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {

        (void)context;
        AssetImportResult result{};
        result.success = true;
        result.message = "[AssetImporter] Scene cook not implemented yet; scene JSON remains authoritative. source=" +
            record.sourcePath.generic_string();
        result.diagnosticsJson = nlohmann::json{
            { "kind", "Scene" },
            { "runtimeLoader", "SceneSerializer" },
            { "cookScene", false },
        }.dump(2);
        return result;
    }

} // namespace HIKARI
