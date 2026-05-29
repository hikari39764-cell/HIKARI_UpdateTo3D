#include "HIKARI_SkyCubemapImporter.h"

#include <Windows.h>

#include <algorithm>
#include <filesystem>
#include <sstream>

#include <json.hpp>

#include "Core/HIKARI_Logger.h"
#include "HIKARI_IblBaker.h"

namespace HIKARI {

    namespace {
        std::string ToLowerCopy(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
                return static_cast<char>(std::tolower(c));
            });
            return value;
        }

        bool IsSkyPath(const std::filesystem::path& path) {
            for (const std::filesystem::path& part : path) {
                const std::string name = ToLowerCopy(part.string());
                if (name == "skies" || name == "sky") {
                    return true;
                }
            }
            return false;
        }

        nlohmann::json MakeDefaultSettings() {
            return nlohmann::json{
                { "sourceDimension", "TextureCube" },
                { "colorSpace", "Linear" },
                { "copySkyCubemap", true },
                { "autoBakeIBL", true },
                { "irradianceSize", 64 },
                { "irradianceSampleCount", 256 },
                { "prefilteredSize", 256 },
                { "prefilteredMipCount", 7 },
                { "prefilteredSampleCount", 1024 },
                { "brdfLutSize", 256 },
                { "brdfSampleCount", 1024 },
                { "outputFormat", "DDS" },
                { "futureOutputFormat", "HTEX" },
            };
        }

        nlohmann::json ReadSettings(const AssetMeta& meta) {
            nlohmann::json settings = nlohmann::json::parse(meta.importSettingsJson, nullptr, false);
            if (!settings.is_object()) {
                settings = MakeDefaultSettings();
            }
            return settings;
        }

        std::filesystem::path MakeProjectRelative(
            const std::filesystem::path& projectRoot,
            const std::filesystem::path& path) {

            std::error_code ec{};
            std::filesystem::path relative = std::filesystem::relative(path, projectRoot, ec);
            if (ec) {
                return path.lexically_normal();
            }
            return relative.lexically_normal();
        }

        bool ReplaceFileWithTemp(
            const std::filesystem::path& tempPath,
            const std::filesystem::path& finalPath,
            std::string& outMessage) {

            const BOOL moved = MoveFileExW(
                tempPath.wstring().c_str(),
                finalPath.wstring().c_str(),
                MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
            if (!moved) {
                std::ostringstream oss;
                oss << "[SkyCubemapImporter] failed to replace output DDS. error=" << GetLastError();
                outMessage = oss.str();
                return false;
            }
            return true;
        }

        bool CopyFileToTemp(
            const std::filesystem::path& sourcePath,
            const std::filesystem::path& tempPath,
            std::string& outMessage) {

            std::error_code ec{};
            std::filesystem::create_directories(tempPath.parent_path(), ec);
            if (ec) {
                outMessage = "[SkyCubemapImporter] failed to create output directory: " + tempPath.parent_path().generic_string();
                return false;
            }

            std::filesystem::copy_file(
                sourcePath,
                tempPath,
                std::filesystem::copy_options::overwrite_existing,
                ec);
            if (ec) {
                outMessage = "[SkyCubemapImporter] failed to copy cubemap: " + ec.message();
                return false;
            }
            return true;
        }
    }

    SkyCubemapImporter::SkyCubemapImporter(std::unique_ptr<ITextureImportBackend> backend)
        : backend_(std::move(backend)) {
    }

    const char* SkyCubemapImporter::GetImporterId() const {
        return "SkyCubemapImporter";
    }

    uint32_t SkyCubemapImporter::GetImporterVersion() const {
        return 1;
    }

    bool SkyCubemapImporter::CanImport(const std::filesystem::path& sourcePath) const {
        return IsSkyPath(sourcePath) && ToLowerCopy(sourcePath.extension().string()) == ".dds";
    }

    AssetMeta SkyCubemapImporter::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {

        AssetMeta meta{};
        meta.metaVersion = 1;
        meta.guid = guid;
        meta.type = AssetType::Sky;
        meta.importerId = GetImporterId();
        meta.importerVersion = GetImporterVersion();
        meta.sourcePath = sourcePath.generic_string();
        meta.displayName = sourcePath.stem().string();
        meta.importSettingsJson = MakeDefaultSettings().dump(2);
        return meta;
    }

    AssetImportResult SkyCubemapImporter::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {

        AssetImportResult result{};
        if (!backend_ || !backend_->IsAvailable()) {
            result.message = "[SkyCubemapImporter] Backend Missing";
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        nlohmann::json settingsJson = ReadSettings(record.meta);
        TextureImportSettings textureSettings{};
        textureSettings.usage = TextureUsage::SkyCubemap;
        textureSettings.dimension = TextureAssetDimension::TextureCube;
        textureSettings.colorSpace = TextureAssetColorSpace::Srgb;
        textureSettings.compression = TextureCompression::None;
        textureSettings.mipPolicy = TextureMipPolicy::Preserve;

        std::string inspectMessage{};
        if (!backend_->Inspect(context.projectRoot / record.sourcePath, textureSettings, inspectMessage)) {
            result.message = inspectMessage.empty() ? "[SkyCubemapImporter] inspect failed" : inspectMessage;
            return result;
        }

        if (textureSettings.dimension != TextureAssetDimension::TextureCube) {
            result.message = "[SkyCubemapImporter] source DDS is not a TextureCube";
            HIKARI_LOG_ERROR(result.message + " source=" + record.sourcePath.generic_string());
            return result;
        }

        const std::filesystem::path finalPath = context.importedDirectory / "sky.dds";
        const std::filesystem::path tempPath = context.importedDirectory / "sky.dds.tmp";
        const bool copySkyCubemap = settingsJson.value("copySkyCubemap", true);

        if (copySkyCubemap) {
            if (!CopyFileToTemp(context.projectRoot / record.sourcePath, tempPath, result.message)) {
                HIKARI_LOG_ERROR(result.message);
                return result;
            }
        } else {
            std::string convertMessage{};
            if (!backend_->ConvertToDds(context.projectRoot / record.sourcePath, tempPath, textureSettings, convertMessage)) {
                result.message = convertMessage.empty() ? "[SkyCubemapImporter] normalize failed" : convertMessage;
                return result;
            }
        }

        if (!ReplaceFileWithTemp(tempPath, finalPath, result.message)) {
            return result;
        }

        result.success = true;
        result.message = "[SkyCubemapImporter] Imported sky cubemap";
        result.artifacts.push_back(AssetArtifactDesc{
            "SkyCubemap",
            MakeProjectRelative(context.projectRoot, finalPath).generic_string(),
            "DDS"
        });

        if (settingsJson.value("autoBakeIBL", true)) {
            IblBakeSettings iblSettings{};
            iblSettings.irradianceSize = settingsJson.value("irradianceSize", 64u);
            iblSettings.prefilteredSize = settingsJson.value("prefilteredSize", 256u);
            iblSettings.prefilteredMipCount = settingsJson.value("prefilteredMipCount", 7u);
            iblSettings.irradianceSampleCount = settingsJson.value("irradianceSampleCount", 256u);
            iblSettings.prefilteredSampleCount = settingsJson.value("prefilteredSampleCount", 1024u);
            iblSettings.brdfLutSize = settingsJson.value("brdfLutSize", 256u);
            iblSettings.brdfSampleCount = settingsJson.value("brdfSampleCount", 1024u);

            const IblBakeResult bake = IblBaker::BakeSkyCubemapToIbl(
                finalPath,
                context.importedDirectory / "IBL",
                context.projectRoot / "Library" / "Generated" / "IBL",
                iblSettings);

            if (bake.success) {
                result.artifacts.push_back(AssetArtifactDesc{
                    "IblIrradiance",
                    MakeProjectRelative(context.projectRoot, bake.irradiancePath).generic_string(),
                    "DDS"
                });
                result.artifacts.push_back(AssetArtifactDesc{
                    "IblPrefiltered",
                    MakeProjectRelative(context.projectRoot, bake.prefilteredPath).generic_string(),
                    "DDS"
                });
                result.artifacts.push_back(AssetArtifactDesc{
                    "BrdfLut",
                    MakeProjectRelative(context.projectRoot, bake.brdfLutPath).generic_string(),
                    "DDS"
                });
                result.message += "; IBL baked";
            } else {
                result.message += "; IBL bake failed: " + bake.message;
                HIKARI_LOG_WARN("[SkyCubemapImporter] IBL bake failed: " + bake.message);
            }
        }

        HIKARI_LOG_INFO(result.message + " source=" + record.sourcePath.generic_string() + " guid=" + record.guid.value);
        return result;
    }

} // namespace HIKARI
