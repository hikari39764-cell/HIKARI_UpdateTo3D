#include "HIKARI_MaterialImporter.h"

#include <algorithm>
#include <cctype>

#include <json.hpp>

#include "Assets/Formats/HIKARI_HmatFormat.h"
#include "Assets/Material/HIKARI_MaterialAssetData.h"
#include "Core/HIKARI_Logger.h"

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

        std::filesystem::path MakeProjectRelative(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& path) {

            std::error_code ec{};
            std::filesystem::path relative = std::filesystem::relative(path, projectRoot, ec);
            if (!ec && !relative.empty()) {
                return relative.lexically_normal();
            }
            return path.lexically_normal();
        }

        nlohmann::json ParseImportSettingsOrDefault(const AssetMeta& meta) {
            nlohmann::json settings = nlohmann::json::parse(meta.importSettingsJson, nullptr, false);
            if (!settings.is_object()) {
                settings = nlohmann::json::object();
            }
            return settings;
        }

        bool ShouldCookHmat(const nlohmann::json& settings) {
            const bool legacyFutureHmat =
                !settings.contains("outputFormat") &&
                settings.value("futureOutputFormat", std::string{}) == "HMAT";
            const bool cookMaterial = legacyFutureHmat
                ? true
                : settings.value("cookMaterial", true);
            const std::string outputFormat = settings.value("outputFormat", std::string("HMAT"));
            return cookMaterial && outputFormat == "HMAT";
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
        return 2;
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
            { "outputFormat", "HMAT" },
            { "cookMaterial", true },
        }.dump(2);
        return meta;
    }

    AssetImportResult MaterialImporter::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {

        AssetImportResult result{};
        if (!IsMaterialJson(record.sourcePath)) {
            result.message = "[AssetImporter] unsupported material source: " + record.sourcePath.generic_string();
            HIKARI_LOG_WARN("[MaterialImporter] unsupported material source: " + record.sourcePath.generic_string());
            return result;
        }

        const std::filesystem::path sourcePath =
            (context.projectRoot / record.sourcePath).lexically_normal();

        PbrMaterialAssetData data{};
        std::string error{};
        if (!LoadPbrMaterialAssetData(sourcePath, data, error)) {
            result.message = "[MaterialImporter][ERROR] Material JSON validation failed. " + error;
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        // Material JSON を検証し、必要なら runtime 用 HMAT へ cook する。
        AddTextureDependency("BaseColor", data.baseColorTexture, result);
        AddTextureDependency("Normal", data.normalTexture, result);
        AddTextureDependency("MetallicRoughness", data.metallicRoughnessTexture, result);
        AddTextureDependency("Occlusion", data.occlusionTexture, result);
        AddTextureDependency("Emissive", data.emissiveTexture, result);

        if (!ShouldCookHmat(ParseImportSettingsOrDefault(record.meta))) {
            result.success = true;
            result.message = "[MaterialImporter] Material JSON validated without cook: " + record.sourcePath.generic_string();
            HIKARI_LOG_INFO("[MaterialImporter] validate only: " + record.sourcePath.generic_string());
            return result;
        }

        const std::filesystem::path outputPath =
            (context.importedDirectory / "material.hmat").lexically_normal();

        std::string writeMessage{};
        if (!WriteHmatFile(outputPath, data, writeMessage)) {
            result.message = "[MaterialImporter][ERROR] HMAT write failed: " + writeMessage;
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        const std::filesystem::path relativeOutput =
            MakeProjectRelative(context.projectRoot, outputPath);
        result.artifacts.push_back(AssetArtifactDesc{
            "Material",
            relativeOutput.generic_string(),
            "HMAT"
        });

        result.success = true;
        result.message = "[MaterialImporter] Material cooked HMAT: " + relativeOutput.generic_string();
        HIKARI_LOG_INFO("[MaterialImporter] cooked HMAT: " + relativeOutput.generic_string());
        return result;
    }

} // namespace HIKARI
