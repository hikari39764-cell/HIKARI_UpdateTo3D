#include "HIKARI_AssetRegistryBuilder.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <system_error>

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

        TextureUsage ParseTextureUsage(const nlohmann::json& settings) {
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
            return TextureUsage::Auto;
        }

        TextureAssetDimension ParseTextureDimension(const nlohmann::json& settings) {
            const std::string value = settings.value("dimension", settings.value("sourceDimension", ""));
            if (value == "TextureCube") return TextureAssetDimension::TextureCube;
            return TextureAssetDimension::Texture2D;
        }

        TextureAssetColorSpace ParseTextureColorSpace(const nlohmann::json& settings) {
            const std::string value = settings.value("colorSpace", "");
            if (value == "Linear") return TextureAssetColorSpace::Linear;
            if (value == "Srgb") return TextureAssetColorSpace::Srgb;
            return TextureAssetColorSpace::Auto;
        }

        TextureCompression ParseTextureCompression(const nlohmann::json& settings) {
            const std::string value = settings.value("compression", "");
            if (value == "None") return TextureCompression::None;
            if (value == "BC1") return TextureCompression::BC1;
            if (value == "BC3") return TextureCompression::BC3;
            if (value == "BC4") return TextureCompression::BC4;
            if (value == "BC5") return TextureCompression::BC5;
            if (value == "BC6H") return TextureCompression::BC6H;
            if (value == "BC7") return TextureCompression::BC7;
            return TextureCompression::Auto;
        }

        TextureMipPolicy ParseTextureMipPolicy(const nlohmann::json& settings) {
            const std::string value = settings.value("mipPolicy", "");
            if (value == "Generate") return TextureMipPolicy::Generate;
            if (value == "Preserve") return TextureMipPolicy::Preserve;
            if (value == "None") return TextureMipPolicy::None;
            return TextureMipPolicy::Auto;
        }

        std::string FindArtifactPath(const AssetRecord& record, std::string_view role) {
            for (const AssetArtifactDesc& artifact : record.meta.artifacts) {
                if (artifact.role == role && !artifact.path.empty()) {
                    return artifact.path;
                }
            }
            return {};
        }

        std::string FindArtifactPathByFormat(
            const AssetRecord& record,
            std::string_view role,
            std::string_view format) {

            for (const AssetArtifactDesc& artifact : record.meta.artifacts) {
                if (artifact.role == role && artifact.format == format && !artifact.path.empty()) {
                    return artifact.path;
                }
            }
            return {};
        }

        bool LoadMaterialSourceJson(
            const AssetDatabase& assetDatabase,
            const AssetRecord& record,
            PbrMaterialAssetData& outData) {

            const std::filesystem::path sourcePath =
                (assetDatabase.GetProjectRoot() / record.sourcePath).lexically_normal();
            std::string loadError{};
            if (LoadPbrMaterialAssetData(sourcePath, outData, loadError)) {
                return true;
            }

            HIKARI_LOG_WARN("[AssetRegistryBuilder][Material] source JSON read failed: " + loadError);
            return false;
        }

        bool LoadMaterialDataForRegistry(
            const AssetDatabase& assetDatabase,
            const AssetRecord& record,
            PbrMaterialAssetData& outData) {

            // Cook 済み HMAT を優先し、無い場合だけ source JSON を読む。
            const std::string hmatArtifact = FindArtifactPathByFormat(record, "Material", "HMAT");
            if (!hmatArtifact.empty()) {
                const std::filesystem::path hmatPath =
                    (assetDatabase.GetProjectRoot() / hmatArtifact).lexically_normal();

                std::string readMessage{};
                if (ReadHmatFile(hmatPath, outData, readMessage)) {
                    HIKARI_LOG_INFO("[AssetRegistryBuilder][Material] loaded HMAT: " + hmatArtifact);
                    return true;
                }

                HIKARI_LOG_WARN("[AssetRegistryBuilder][Material][WARN] HMAT read failed: " + readMessage);
                return LoadMaterialSourceJson(assetDatabase, record, outData);
            }

            // 旧 asset 互換のため JSON 読み込み経路は残す。
            HIKARI_LOG_WARN("[AssetRegistryBuilder][Material] HMAT artifact missing, fallback to source JSON: " +
                record.sourcePath.generic_string());
            return LoadMaterialSourceJson(assetDatabase, record, outData);
        }

        std::string FindSharedBrdfLutPath(const AssetDatabase& assetDatabase) {
            const std::filesystem::path relativePath =
                std::filesystem::path("Library") / "Generated" / "IBL" / "brdf_lut.dds";
            const std::filesystem::path absolutePath =
                (assetDatabase.GetProjectRoot() / relativePath).lexically_normal();

            std::error_code ec{};
            if (std::filesystem::exists(absolutePath, ec) && !ec) {
                return relativePath.generic_string();
            }

            return {};
        }

        ModelImporterKind GuessModelImporter(const std::filesystem::path& sourcePath) {
            const std::string ext = ToLowerCopy(sourcePath.extension().string());
            if (ext == ".gltf") {
                return ModelImporterKind::Gltf;
            }
            if (ext == ".obj" || ext == ".fbx") {
                return ModelImporterKind::Assimp;
            }
            return ModelImporterKind::Gltf;
        }

        nlohmann::json ReadImportSettings(const AssetRecord& record) {
            nlohmann::json settings = nlohmann::json::parse(record.meta.importSettingsJson, nullptr, false);
            return settings.is_object() ? settings : nlohmann::json::object();
        }

        const nlohmann::json& ClusterGeometrySettingsOrEmpty(const nlohmann::json& settings) {
            static const nlohmann::json empty = nlohmann::json::object();
            if (settings.contains("clusterGeometry") && settings["clusterGeometry"].is_object()) {
                return settings["clusterGeometry"];
            }
            return empty;
        }

        ModelGeometryCookProfile ParseModelGeometryCookProfile(const nlohmann::json& settings) {
            const nlohmann::json& cluster = ClusterGeometrySettingsOrEmpty(settings);
            const std::string value = cluster.value(
                "profile",
                settings.value("geometryProfile", settings.value("clusterGeometryProfile", "Scene")));
            if (value == "Character" || value == "character") {
                return ModelGeometryCookProfile::Character;
            }
            return ModelGeometryCookProfile::Scene;
        }

        bool ReadClusterBool(
            const nlohmann::json& settings,
            const nlohmann::json& cluster,
            const char* key,
            bool fallback) {

            if (cluster.contains(key)) {
                return cluster.value(key, fallback);
            }
            return settings.value(key, fallback);
        }

        uint32_t ReadClusterUint(
            const nlohmann::json& settings,
            const nlohmann::json& cluster,
            const char* key,
            uint32_t fallback,
            uint32_t minimum,
            uint32_t maximum) {

            uint32_t value = fallback;
            if (cluster.contains(key)) {
                value = cluster.value(key, fallback);
            } else {
                value = settings.value(key, fallback);
            }
            return (std::max)(minimum, (std::min)(value, maximum));
        }

        float ReadClusterFloat(
            const nlohmann::json& settings,
            const nlohmann::json& cluster,
            const char* key,
            float fallback,
            float minimum,
            float maximum) {

            float value = fallback;
            if (cluster.contains(key)) {
                value = cluster.value(key, fallback);
            } else {
                value = settings.value(key, fallback);
            }
            return (std::max)(minimum, (std::min)(value, maximum));
        }

        ModelClusterCookOptions ParseModelClusterCookOptions(const nlohmann::json& settings) {
            const nlohmann::json& cluster = ClusterGeometrySettingsOrEmpty(settings);
            ModelClusterCookOptions options{};
            options.profile = ParseModelGeometryCookProfile(settings);
            if (options.profile == ModelGeometryCookProfile::Character) {
                options.largeSurfaceTargetExtent = 1.25f;
            } else {
                options.largeSurfaceTargetExtent = 3.0f;
            }
            options.buildClusterGeometry = ReadClusterBool(settings, cluster, "enabled", true);
            options.maxLodCount = ReadClusterUint(settings, cluster, "maxLodCount", options.maxLodCount, 1u, 5u);
            options.lodQualityBias = ReadClusterFloat(settings, cluster, "lodQualityBias", options.lodQualityBias, 0.25f, 4.0f);
            options.partitionLargeSurfaces = ReadClusterBool(settings, cluster, "partitionLargeSurfaces", options.partitionLargeSurfaces);
            options.largeSurfaceTargetExtent = ReadClusterFloat(
                settings,
                cluster,
                "largeSurfaceTargetExtent",
                options.largeSurfaceTargetExtent,
                0.50f,
                64.0f);
            options.lockPartitionBorders = ReadClusterBool(settings, cluster, "lockPartitionBorders", options.lockPartitionBorders);
            return options;
        }
    }

    bool AssetRegistryBuilder::AppendToRegistry(const AssetDatabase& assetDatabase, AssetRegistry& registry) const {
        bool ok = true;

        for (const AssetRecord* record : assetDatabase.CollectAll()) {
            if (!record || !record->guid.IsValid() || record->duplicateGuid) {
                continue;
            }

            if (record->type == AssetType::Texture) {
                auto descriptor = std::make_unique<TextureAssetDescriptor>();
                descriptor->id.value = record->guid.value;
                descriptor->type = AssetType::Texture;
                descriptor->sourcePath = FindArtifactPathByFormat(*record, "MainTexture", "HTEX");
                if (descriptor->sourcePath.empty()) {
                    descriptor->sourcePath = FindArtifactPath(*record, "MainTexture");
                }
                if (descriptor->sourcePath.empty()) {
                    descriptor->sourcePath = record->sourcePath.generic_string();
                }
                descriptor->version = record->meta.importerVersion;

                nlohmann::json settings = nlohmann::json::parse(record->meta.importSettingsJson, nullptr, false);
                if (settings.is_object()) {
                    descriptor->dimension = ParseTextureDimension(settings);
                    descriptor->colorSpace = ParseTextureColorSpace(settings);
                    descriptor->usage = ParseTextureUsage(settings);
                    descriptor->compression = ParseTextureCompression(settings);
                    descriptor->mipPolicy = ParseTextureMipPolicy(settings);
                }
                ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
            } else if (record->type == AssetType::Model) {
                auto descriptor = std::make_unique<ModelAssetDescriptor>();
                descriptor->id.value = record->guid.value;
                descriptor->type = AssetType::Model;
                descriptor->sourcePath = FindArtifactPathByFormat(*record, "MainModel", "HMODEL");
                if (descriptor->sourcePath.empty()) {
                    descriptor->sourcePath = FindArtifactPath(*record, "MainModel");
                }
                if (descriptor->sourcePath.empty()) {
                    descriptor->sourcePath = record->sourcePath.generic_string();
                }
                descriptor->clusteredGeometryPath =
                    FindArtifactPathByFormat(*record, "ClusteredGeometry", "HCMESH");
                descriptor->version = record->meta.importerVersion;
                descriptor->importer = GuessModelImporter(record->sourcePath);
                const nlohmann::json settings = ReadImportSettings(*record);
                descriptor->importOptions.loadMaterials = settings.value("loadMaterials", true);
                descriptor->importOptions.loadTextures = settings.value("loadTextures", true);
                descriptor->importOptions.clusterGeometry = ParseModelClusterCookOptions(settings);
                ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
            } else if (record->type == AssetType::Sky) {
                auto descriptor = std::make_unique<SkyAssetDescriptor>();
                descriptor->id.value = record->guid.value;
                descriptor->type = AssetType::Sky;
                const std::string skyArtifact = FindArtifactPath(*record, "SkyCubemap");
                if (skyArtifact.empty()) {
                    HIKARI_LOG_WARN("[AssetRegistryBuilder][Sky] missing SkyCubemap artifact, fallback to source: " +
                        record->sourcePath.generic_string());
                    descriptor->sourcePath = record->sourcePath.generic_string();
                } else {
                    descriptor->sourcePath = skyArtifact;
                }
                descriptor->version = record->meta.importerVersion;
                descriptor->textureAssetId.clear();
                descriptor->preferredMode = SkyMode::Cubemap;
                descriptor->irradiancePath = FindArtifactPath(*record, "IblIrradiance");
                descriptor->prefilteredPath = FindArtifactPath(*record, "IblPrefiltered");
                descriptor->brdfLutPath = FindArtifactPath(*record, "BrdfLut");
                if (descriptor->brdfLutPath.empty()) {
                    descriptor->brdfLutPath = FindSharedBrdfLutPath(assetDatabase);
                }
                descriptor->hasIbl =
                    !descriptor->irradiancePath.empty() ||
                    !descriptor->prefilteredPath.empty();

                nlohmann::json settings = nlohmann::json::parse(record->meta.importSettingsJson, nullptr, false);
                if (settings.is_object()) {
                    descriptor->prefilteredMipCount = settings.value("prefilteredMipCount", 7u);
                }
                ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
            } else if (record->type == AssetType::Material) {
                auto descriptor = std::make_unique<MaterialAssetDescriptor>();
                descriptor->id.value = record->guid.value;
                descriptor->type = AssetType::Material;
                descriptor->sourcePath = FindArtifactPathByFormat(*record, "Material", "HMAT");
                if (descriptor->sourcePath.empty()) {
                    descriptor->sourcePath = record->sourcePath.generic_string();
                }
                descriptor->version = record->meta.importerVersion;

                if (!LoadMaterialDataForRegistry(assetDatabase, *record, descriptor->data)) {
                    descriptor->data.materialName = record->displayName.empty()
                        ? record->sourcePath.stem().string()
                        : record->displayName;
                    HIKARI_LOG_WARN("[AssetRegistryBuilder][Material] using fallback material data: " +
                        descriptor->data.materialName);
                }
                ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
            } else if (record->type == AssetType::VfxEffect) {
                auto descriptor = std::make_unique<VfxAssetDescriptor>();
                descriptor->id.value = record->guid.value;
                descriptor->type = AssetType::VfxEffect;
                descriptor->sourcePath = record->sourcePath.generic_string();
                descriptor->version = record->meta.importerVersion;
                ok = registry.RegisterDescriptor(std::move(descriptor)) && ok;
            }
        }

        return ok;
    }

} // namespace HIKARI
