#include "HIKARI_TextureImporter.h"

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <exception>
#include <filesystem>
#include <sstream>

#include <json.hpp>

#include "Core/HIKARI_Logger.h"
#include "HIKARI_HtexTextureWriter_DirectXTex.h"

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

        bool IsTextureExtension(const std::string& ext) {
            return ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                ext == ".tga" || ext == ".bmp" || ext == ".dds" ||
                ext == ".hdr";
        }

        const char* ToString(TextureUsage value) {
            switch (value) {
            case TextureUsage::BaseColor: return "BaseColor";
            case TextureUsage::Normal: return "Normal";
            case TextureUsage::MetallicRoughness: return "MetallicRoughness";
            case TextureUsage::Occlusion: return "Occlusion";
            case TextureUsage::Emissive: return "Emissive";
            case TextureUsage::Mask: return "Mask";
            case TextureUsage::UI: return "UI";
            case TextureUsage::SkyCubemap: return "SkyCubemap";
            case TextureUsage::IblIrradiance: return "IblIrradiance";
            case TextureUsage::IblPrefiltered: return "IblPrefiltered";
            case TextureUsage::BrdfLut: return "BrdfLut";
            case TextureUsage::Auto:
            default: return "Auto";
            }
        }

        const char* ToString(TextureAssetDimension value) {
            switch (value) {
            case TextureAssetDimension::TextureCube: return "TextureCube";
            case TextureAssetDimension::Texture2D:
            default: return "Texture2D";
            }
        }

        const char* ToString(TextureAssetColorSpace value) {
            switch (value) {
            case TextureAssetColorSpace::Linear: return "Linear";
            case TextureAssetColorSpace::Srgb: return "Srgb";
            case TextureAssetColorSpace::Auto:
            default: return "Auto";
            }
        }

        const char* ToString(TextureCompression value) {
            switch (value) {
            case TextureCompression::None: return "None";
            case TextureCompression::BC1: return "BC1";
            case TextureCompression::BC3: return "BC3";
            case TextureCompression::BC4: return "BC4";
            case TextureCompression::BC5: return "BC5";
            case TextureCompression::BC6H: return "BC6H";
            case TextureCompression::BC7: return "BC7";
            case TextureCompression::Auto:
            default: return "Auto";
            }
        }

        const char* ToString(TextureMipPolicy value) {
            switch (value) {
            case TextureMipPolicy::Generate: return "Generate";
            case TextureMipPolicy::Preserve: return "Preserve";
            case TextureMipPolicy::None: return "None";
            case TextureMipPolicy::Auto:
            default: return "Auto";
            }
        }

        TextureUsage ParseTextureUsage(const nlohmann::json& settings, TextureUsage fallback) {
            const std::string value = settings.value("usage", "");
            if (value == "BaseColor") return TextureUsage::BaseColor;
            if (value == "Normal") return TextureUsage::Normal;
            if (value == "MetallicRoughness") return TextureUsage::MetallicRoughness;
            if (value == "Occlusion") return TextureUsage::Occlusion;
            if (value == "Emissive") return TextureUsage::Emissive;
            if (value == "Mask") return TextureUsage::Mask;
            if (value == "UI") return TextureUsage::UI;
            if (value == "SkyCubemap") return TextureUsage::SkyCubemap;
            if (value == "IblIrradiance") return TextureUsage::IblIrradiance;
            if (value == "IblPrefiltered") return TextureUsage::IblPrefiltered;
            if (value == "BrdfLut") return TextureUsage::BrdfLut;
            if (value == "Auto") return TextureUsage::Auto;
            return fallback;
        }

        TextureAssetDimension ParseTextureDimension(const nlohmann::json& settings, TextureAssetDimension fallback) {
            const std::string value = settings.value("dimension", "");
            if (value == "TextureCube") return TextureAssetDimension::TextureCube;
            if (value == "Texture2D") return TextureAssetDimension::Texture2D;
            return fallback;
        }

        TextureAssetColorSpace ParseTextureColorSpace(const nlohmann::json& settings, TextureAssetColorSpace fallback) {
            const std::string value = settings.value("colorSpace", "");
            if (value == "Linear") return TextureAssetColorSpace::Linear;
            if (value == "Srgb") return TextureAssetColorSpace::Srgb;
            if (value == "Auto") return TextureAssetColorSpace::Auto;
            return fallback;
        }

        TextureCompression ParseTextureCompression(const nlohmann::json& settings, TextureCompression fallback) {
            const std::string value = settings.value("compression", "");
            if (value == "None") return TextureCompression::None;
            if (value == "BC1") return TextureCompression::BC1;
            if (value == "BC3") return TextureCompression::BC3;
            if (value == "BC4") return TextureCompression::BC4;
            if (value == "BC5") return TextureCompression::BC5;
            if (value == "BC6H") return TextureCompression::BC6H;
            if (value == "BC7") return TextureCompression::BC7;
            if (value == "Auto") return TextureCompression::Auto;
            return fallback;
        }

        TextureMipPolicy ParseTextureMipPolicy(const nlohmann::json& settings, TextureMipPolicy fallback) {
            const std::string value = settings.value("mipPolicy", "");
            if (value == "Generate") return TextureMipPolicy::Generate;
            if (value == "Preserve") return TextureMipPolicy::Preserve;
            if (value == "None") return TextureMipPolicy::None;
            if (value == "Auto") return TextureMipPolicy::Auto;
            return fallback;
        }

        TextureImportSettings GuessSettings(const std::filesystem::path& sourcePath) {
            TextureImportSettings settings{};
            settings.dimension = TextureAssetDimension::Texture2D;
            settings.outputFormat = CookedAssetFormat::DDS;

            const std::string key = ToLowerCopy(sourcePath.stem().string() + " " + sourcePath.generic_string());
            const std::string ext = ToLowerCopy(sourcePath.extension().string());

            if (key.find("basecolor") != std::string::npos ||
                key.find("base_color") != std::string::npos ||
                key.find("albedo") != std::string::npos ||
                key.find("diffuse") != std::string::npos ||
                key.find("_color") != std::string::npos ||
                EndsWith(key, " color")) {
                settings.usage = TextureUsage::BaseColor;
                settings.colorSpace = TextureAssetColorSpace::Srgb;
                settings.compression = TextureCompression::BC7;
                settings.mipPolicy = TextureMipPolicy::Generate;
            } else if (key.find("normal") != std::string::npos ||
                key.find("_nrm") != std::string::npos ||
                key.find("_n.") != std::string::npos) {
                settings.usage = TextureUsage::Normal;
                settings.colorSpace = TextureAssetColorSpace::Linear;
                settings.compression = TextureCompression::BC5;
                settings.mipPolicy = TextureMipPolicy::Generate;
            } else if (key.find("metallicroughness") != std::string::npos ||
                key.find("metallic_roughness") != std::string::npos ||
                key.find("roughness") != std::string::npos ||
                key.find("metallic") != std::string::npos ||
                key.find("_orm") != std::string::npos ||
                key.find("_ao") != std::string::npos ||
                key.find("ambientocclusion") != std::string::npos ||
                key.find("occlusion") != std::string::npos) {
                settings.usage = TextureUsage::MetallicRoughness;
                settings.colorSpace = TextureAssetColorSpace::Linear;
                settings.compression = TextureCompression::BC7;
                settings.mipPolicy = TextureMipPolicy::Generate;
            } else if (key.find("emissive") != std::string::npos) {
                settings.usage = TextureUsage::Emissive;
                settings.colorSpace = TextureAssetColorSpace::Srgb;
                settings.compression = TextureCompression::BC7;
                settings.mipPolicy = TextureMipPolicy::Generate;
            } else if (key.find("mask") != std::string::npos) {
                settings.usage = TextureUsage::Mask;
                settings.colorSpace = TextureAssetColorSpace::Linear;
                settings.compression = TextureCompression::BC7;
                settings.mipPolicy = TextureMipPolicy::Generate;
            } else {
                settings.usage = TextureUsage::Auto;
                settings.colorSpace = TextureAssetColorSpace::Srgb;
                settings.compression = TextureCompression::BC7;
                settings.mipPolicy = TextureMipPolicy::Generate;
            }

            if (ext == ".dds") {
                settings.colorSpace = TextureAssetColorSpace::Auto;
                settings.mipPolicy = TextureMipPolicy::Preserve;
                settings.compression = TextureCompression::None;
            }
            if (ext == ".hdr") {
                settings.colorSpace = TextureAssetColorSpace::Linear;
                settings.compression = TextureCompression::None;
            }

            return settings;
        }

        TextureImportSettings ReadSettings(const AssetMeta& meta) {
            TextureImportSettings settings = GuessSettings(meta.sourcePath);

            nlohmann::json root = nlohmann::json::parse(meta.importSettingsJson, nullptr, false);
            if (!root.is_discarded() && root.is_object()) {
                settings.usage = ParseTextureUsage(root, settings.usage);
                settings.dimension = ParseTextureDimension(root, settings.dimension);
                settings.colorSpace = ParseTextureColorSpace(root, settings.colorSpace);
                settings.compression = ParseTextureCompression(root, settings.compression);
                settings.mipPolicy = ParseTextureMipPolicy(root, settings.mipPolicy);
                settings.forcePowerOfTwo = root.value("forcePowerOfTwo", settings.forcePowerOfTwo);
                settings.allowResize = root.value("allowResize", settings.allowResize);
                settings.maxSize = root.value("maxSize", settings.maxSize);
            }

            if (settings.usage == TextureUsage::Normal) {
                settings.colorSpace = TextureAssetColorSpace::Linear;
                if (settings.compression == TextureCompression::Auto) {
                    settings.compression = TextureCompression::BC5;
                }
            }

            return settings;
        }

        nlohmann::json MakeSettingsJson(const TextureImportSettings& settings) {
            return nlohmann::json{
                { "usage", ToString(settings.usage) },
                { "dimension", ToString(settings.dimension) },
                { "colorSpace", ToString(settings.colorSpace) },
                { "mipPolicy", ToString(settings.mipPolicy) },
                { "compression", ToString(settings.compression) },
                { "outputFormat", "HTEX" },
                { "debugOutputFormat", "DDS" },
                { "forcePowerOfTwo", settings.forcePowerOfTwo },
                { "allowResize", settings.allowResize },
                { "maxSize", settings.maxSize },
            };
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
                const DWORD error = GetLastError();
                std::error_code removeEc{};
                std::filesystem::remove(tempPath, removeEc);

                std::ostringstream oss;
                oss << "[TextureImporter] failed to replace output artifact. error=" << error
                    << " temp=" << tempPath.generic_string()
                    << " final=" << finalPath.generic_string();
                outMessage = oss.str();
                HIKARI_LOG_ERROR(outMessage);
                return false;
            }
            return true;
        }
    }

    TextureImporter::TextureImporter(std::unique_ptr<ITextureImportBackend> backend)
        : backend_(std::move(backend)) {
    }

    const char* TextureImporter::GetImporterId() const {
        return "TextureImporter";
    }

    uint32_t TextureImporter::GetImporterVersion() const {
        return 2;
    }

    bool TextureImporter::CanImport(const std::filesystem::path& sourcePath) const {
        return IsTextureExtension(ToLowerCopy(sourcePath.extension().string()));
    }

    AssetMeta TextureImporter::CreateDefaultMeta(
        const std::filesystem::path& sourcePath,
        const AssetGuid& guid) const {

        TextureImportSettings settings = GuessSettings(sourcePath);

        AssetMeta meta{};
        meta.metaVersion = 1;
        meta.guid = guid;
        meta.type = AssetType::Texture;
        meta.importerId = GetImporterId();
        meta.importerVersion = GetImporterVersion();
        meta.sourcePath = sourcePath.generic_string();
        meta.displayName = sourcePath.stem().string();
        meta.importSettingsJson = MakeSettingsJson(settings).dump(2);
        return meta;
    }

    AssetImportResult TextureImporter::Import(
        const AssetRecord& record,
        const AssetImportContext& context) {

        AssetImportResult result{};
        if (!backend_ || !backend_->IsAvailable()) {
            result.message = "[TextureImporter] Backend Missing";
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        TextureImportSettings settings = ReadSettings(record.meta);
        std::string inspectMessage{};
        try {
            if (!backend_->Inspect(context.projectRoot / record.sourcePath, settings, inspectMessage)) {
                result.message = inspectMessage.empty() ? "[TextureImporter] inspect failed" : inspectMessage;
                return result;
            }
        } catch (const std::exception& ex) {
            result.message = std::string("[TextureImporter] inspect exception: ") + ex.what();
            HIKARI_LOG_ERROR(result.message);
            return result;
        } catch (...) {
            result.message = "[TextureImporter] inspect exception: unknown";
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        const std::filesystem::path finalPath = context.importedDirectory / "texture.dds";
        const std::filesystem::path tempPath = context.importedDirectory / "texture.importing.dds";
        const std::filesystem::path finalHtexPath = context.importedDirectory / "texture.htex";
        const std::filesystem::path tempHtexPath = context.importedDirectory / "texture.importing.htex";

        std::error_code removeEc{};
        std::filesystem::remove(tempPath, removeEc);
        if (removeEc) {
            result.message = "[TextureImporter] failed to clear stale temporary DDS: " + tempPath.generic_string();
            HIKARI_LOG_ERROR(result.message);
            return result;
        }
        removeEc.clear();
        std::filesystem::remove(tempHtexPath, removeEc);
        if (removeEc) {
            result.message = "[TextureImporter] failed to clear stale temporary HTEX: " + tempHtexPath.generic_string();
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        std::string convertMessage{};
        try {
            if (!backend_->ConvertToDds(context.projectRoot / record.sourcePath, tempPath, settings, convertMessage)) {
                std::error_code cleanupEc{};
                std::filesystem::remove(tempPath, cleanupEc);
                result.message = convertMessage.empty() ? "[TextureImporter] import failed" : convertMessage;
                return result;
            }
        } catch (const std::exception& ex) {
            std::error_code cleanupEc{};
            std::filesystem::remove(tempPath, cleanupEc);
            result.message = std::string("[TextureImporter] import exception: ") + ex.what();
            HIKARI_LOG_ERROR(result.message);
            return result;
        } catch (...) {
            std::error_code cleanupEc{};
            std::filesystem::remove(tempPath, cleanupEc);
            result.message = "[TextureImporter] import exception: unknown";
            HIKARI_LOG_ERROR(result.message);
            return result;
        }

        std::string htexMessage{};
        if (!WriteHtexFromDdsWithDirectXTex(tempPath, tempHtexPath, settings, htexMessage)) {
            std::error_code cleanupEc{};
            std::filesystem::remove(tempHtexPath, cleanupEc);
            std::filesystem::remove(tempPath, cleanupEc);
            result.message = htexMessage.empty() ? "[TextureImporter] HTEX conversion failed" : htexMessage;
            return result;
        }

        if (!ReplaceFileWithTemp(tempHtexPath, finalHtexPath, result.message)) {
            std::error_code cleanupEc{};
            std::filesystem::remove(tempPath, cleanupEc);
            return result;
        }

        if (!ReplaceFileWithTemp(tempPath, finalPath, result.message)) {
            return result;
        }

        result.success = true;
        result.message = "[TextureImporter] Imported with DirectXTex and wrote HTEX";
        result.artifacts.push_back(AssetArtifactDesc{
            "MainTexture",
            MakeProjectRelative(context.projectRoot, finalHtexPath).generic_string(),
            "HTEX"
        });
        result.artifacts.push_back(AssetArtifactDesc{
            "DebugDDS",
            MakeProjectRelative(context.projectRoot, finalPath).generic_string(),
            "DDS"
        });

        HIKARI_LOG_INFO(result.message + " source=" + record.sourcePath.generic_string() + " guid=" + record.guid.value);
        return result;
    }

} // namespace HIKARI
