#include "HIKARI_ModelImporterStub.h"

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

    const char* ModelImporterStub::GetImporterId() const {
        return "ModelImporterStub";
    }

    uint32_t ModelImporterStub::GetImporterVersion() const {
        return 1;
    }

    bool ModelImporterStub::CanImport(const std::filesystem::path& sourcePath) const {
        const std::string ext = ToLowerCopy(sourcePath.extension().string());
        return ext == ".gltf" || ext == ".glb" || ext == ".fbx" || ext == ".obj";
    }

    AssetMeta ModelImporterStub::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {

        AssetMeta meta{};
        meta.metaVersion = 1;
        meta.guid = guid;
        meta.type = AssetType::Model;
        meta.importerId = GetImporterId();
        meta.importerVersion = GetImporterVersion();
        meta.sourcePath = sourcePath.generic_string();
        meta.displayName = sourcePath.stem().string();
        meta.importSettingsJson = nlohmann::json{
            { "sourceFormat", sourcePath.extension().string() },
            { "futureOutputFormat", "HMESH" },
            { "cookModel", false },
        }.dump(2);
        return meta;
    }

    AssetImportResult ModelImporterStub::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {

        (void)context;
        AssetImportResult result{};
        result.success = true;
        result.message = "[AssetImporter] Model cook not implemented yet; existing ModelManager path remains authoritative. source=" +
            record.sourcePath.generic_string();
        return result;
    }

} // namespace HIKARI
