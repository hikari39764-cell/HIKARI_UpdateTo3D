#include "HIKARI_MaterialImporter.h"

#include <algorithm>
#include <cctype>

#include <json.hpp>

#include "Assets/Material/HIKARI_MaterialAssetData.h"

namespace HIKARI {

    namespace {
        std::string ToLowerCopy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        bool IsMaterialJson(const std::filesystem::path& sourcePath) {
            const std::string filename = ToLowerCopy(sourcePath.filename().string());
            return filename.ends_with(".material.json");
        }

        void AddTextureDependency(
            std::string role,
            const MaterialTextureSlotData& slot,
            AssetImportResult& result) {

            if (!slot.useTexture || !slot.textureAssetGuid.IsValid()) {
                return;
            }

            result.dependencies.push_back(AssetDependencyDesc{
                slot.textureAssetGuid,
                {},
                std::move(role)
            });
        }
    }

    const char* MaterialImporter::GetImporterId() const {
        return "MaterialImporter";
    }

    uint32_t MaterialImporter::GetImporterVersion() const {
        return 1;
    }

    bool MaterialImporter::CanImport(const std::filesystem::path& sourcePath) const {
        return IsMaterialJson(sourcePath);
    }

    AssetMeta MaterialImporter::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {

        AssetMeta meta{};
        meta.metaVersion = 1;
        meta.guid = guid;
        meta.type = AssetType::Material;
        meta.importerId = GetImporterId();
        meta.importerVersion = GetImporterVersion();
        meta.sourcePath = sourcePath.generic_string();
        meta.displayName = sourcePath.stem().stem().string();
        meta.importSettingsJson = nlohmann::json{
            { "shaderModel", "PBR" },
            { "sourceFormat", ".material.json" },
            { "futureOutputFormat", "HMAT" },
            { "cookMaterial", false },
        }.dump(2);
        return meta;
    }

    AssetImportResult MaterialImporter::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {

        AssetImportResult result{};
        if (!IsMaterialJson(record.sourcePath)) {
            result.message = "[AssetImporter] unsupported material source: " + record.sourcePath.generic_string();
            return result;
        }

        const std::filesystem::path sourcePath =
            (context.projectRoot / record.sourcePath).lexically_normal();

        PbrMaterialAssetData data{};
        std::string error{};
        if (!LoadPbrMaterialAssetData(sourcePath, data, error)) {
            result.message = "[AssetImporter] Material JSON validation failed. " + error;
            return result;
        }

        // Material importer は cook せず、GUID 依存だけを meta に戻す。
        AddTextureDependency("BaseColor", data.baseColorTexture, result);
        AddTextureDependency("Normal", data.normalTexture, result);
        AddTextureDependency("MetallicRoughness", data.metallicRoughnessTexture, result);
        AddTextureDependency("Occlusion", data.occlusionTexture, result);
        AddTextureDependency("Emissive", data.emissiveTexture, result);

        result.success = true;
        result.message = "[AssetImporter] Material JSON validated: " + record.sourcePath.generic_string();
        return result;
    }

} // namespace HIKARI
