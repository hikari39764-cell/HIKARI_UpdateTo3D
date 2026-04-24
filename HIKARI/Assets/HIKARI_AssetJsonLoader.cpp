#include "HIKARI_AssetJsonLoader.h"

#include <fstream>
#include <optional>

#include <Windows.h>
#include <json.hpp>

#include "HIKARI_AssetRegistry.h"

namespace HIKARI {

    using nlohmann::json;

    namespace {
        void LogAssetError(const std::string& path, const std::string& message) {
            std::string text = "[AssetJsonLoader] " + path + ": " + message + "\n";
            OutputDebugStringA(text.c_str());
        }

        AssetType ParseAssetType(const std::string& type) {
            if (type == "Model") return AssetType::Model;
            if (type == "Sky") return AssetType::Sky;
            if (type == "Texture") return AssetType::Texture;
            if (type == "Material") return AssetType::Material;
            if (type == "Animation") return AssetType::Animation;
            if (type == "Particle") return AssetType::Particle;
            if (type == "VfxEffect") return AssetType::VfxEffect;
            return AssetType::Unknown;
        }

        bool ReadJson(const std::string& path, json& outRoot) {
            std::ifstream ifs(path);
            if (!ifs.is_open()) {
                return false;
            }

            outRoot = json::parse(ifs, nullptr, false);
            return !outRoot.is_discarded() && outRoot.is_object();
        }

        std::optional<ModelImporterKind> ParseImporter(const std::string& importer) {
            if (importer == "builtin") return ModelImporterKind::Builtin;
            if (importer == "gltf") return ModelImporterKind::Gltf;
            if (importer == "assimp") return ModelImporterKind::Assimp;
            return std::nullopt;
        }

        CoordinateSystem ParseCoordinateSystem(const std::string& value) {
            if (value == "LeftHanded_YUp") return CoordinateSystem::LeftHanded_YUp;
            return CoordinateSystem::RightHanded_YUp;
        }

        NormalImportPolicy ParseNormalPolicy(const std::string& value) {
            if (value == "Require") return NormalImportPolicy::Require;
            if (value == "Always") return NormalImportPolicy::Always;
            return NormalImportPolicy::IfMissing;
        }

        TangentImportPolicy ParseTangentPolicy(const std::string& value) {
            if (value == "None") return TangentImportPolicy::None;
            if (value == "Always") return TangentImportPolicy::Always;
            return TangentImportPolicy::IfMissing;
        }
    }

    bool AssetJsonLoader::LoadModelDescriptors(const std::string& path, AssetRegistry& registry) const {
        json root;
        if (!ReadJson(path, root)) {
            LogAssetError(path, "failed to parse JSON");
            return false;
        }

        const uint32_t version = root.value("version", 0u);
        if (version != 2u) {
            LogAssetError(path, "models JSON must use version 2");
            return false;
        }

        if (!root.contains("models") || !root["models"].is_array()) {
            LogAssetError(path, "missing models array");
            return false;
        }

        for (const json& node : root["models"]) {
            if (!node.is_object()) {
                continue;
            }

            if (!node.contains("id") || !node.contains("sourcePath") || !node.contains("importer") || !node.contains("importOptions")) {
                LogAssetError(path, "skip model due to missing required fields(id/sourcePath/importer/importOptions)");
                continue;
            }

            auto descriptor = std::make_unique<ModelAssetDescriptor>();
            descriptor->id.value = node.value("id", "");
            descriptor->type = ParseAssetType(node.value("type", "Model"));
            descriptor->sourcePath = node.value("sourcePath", "");
            descriptor->version = 2;
            descriptor->preload = node.value("preload", true);

            const auto importer = ParseImporter(node.value("importer", ""));
            if (!importer.has_value()) {
                LogAssetError(path, "skip model '" + descriptor->id.value + "': importer must be one of builtin/gltf/assimp");
                continue;
            }
            descriptor->importer = *importer;

            const json& options = node["importOptions"];
            if (!options.is_object()) {
                LogAssetError(path, "skip model '" + descriptor->id.value + "': importOptions must be object");
                continue;
            }

            descriptor->importOptions.unitScale = options.value("unitScale", descriptor->importOptions.unitScale);
            descriptor->importOptions.coordinateSystem = ParseCoordinateSystem(options.value("coordinateSystem", "RightHanded_YUp"));
            descriptor->importOptions.generateNormals = ParseNormalPolicy(options.value("generateNormals", "IfMissing"));
            descriptor->importOptions.generateTangents = ParseTangentPolicy(options.value("generateTangents", "IfMissing"));
            descriptor->importOptions.triangulate = options.value("triangulate", descriptor->importOptions.triangulate);
            descriptor->importOptions.flipUV = options.value("flipUV", descriptor->importOptions.flipUV);
            descriptor->importOptions.mergeMeshes = options.value("mergeMeshes", descriptor->importOptions.mergeMeshes);
            descriptor->importOptions.keepNodeHierarchy = options.value("keepNodeHierarchy", descriptor->importOptions.keepNodeHierarchy);
            descriptor->importOptions.loadMaterials = options.value("loadMaterials", descriptor->importOptions.loadMaterials);
            descriptor->importOptions.loadTextures = options.value("loadTextures", descriptor->importOptions.loadTextures);
            descriptor->importOptions.loadAnimations = options.value("loadAnimations", descriptor->importOptions.loadAnimations);
            descriptor->importOptions.loadSkins = options.value("loadSkins", descriptor->importOptions.loadSkins);

            if (node.contains("animation") && node["animation"].is_object()) {
                const auto& animation = node["animation"];
                descriptor->animation.defaultClip = animation.value("defaultClip", "");
                if (animation.contains("clips") && animation["clips"].is_array()) {
                    for (const auto& clipNode : animation["clips"]) {
                        if (!clipNode.is_object()) {
                            continue;
                        }
                        AnimationClipAlias alias{};
                        alias.name = clipNode.value("name", "");
                        alias.sourceName = clipNode.value("sourceName", "");
                        if (!alias.name.empty()) {
                            descriptor->animation.clips.push_back(std::move(alias));
                        }
                    }
                }
            }

            if (node.contains("materialOverrides") && node["materialOverrides"].is_array()) {
                for (const auto& overrideNode : node["materialOverrides"]) {
                    if (!overrideNode.is_object()) {
                        continue;
                    }
                    ModelMaterialOverrideDesc materialOverride{};
                    materialOverride.targetMaterialName = overrideNode.value("targetMaterialName", "");
                    materialOverride.shaderProfileId = overrideNode.value("shaderProfileId", "");
                    materialOverride.materialFxProfileId = overrideNode.value("materialFxProfileId", "");
                    materialOverride.doubleSided = overrideNode.value("doubleSided", false);
                    descriptor->materialOverrides.push_back(std::move(materialOverride));
                }
            }

            registry.RegisterDescriptor(std::move(descriptor));
        }

        return true;
    }

    bool AssetJsonLoader::LoadSkyDescriptors(const std::string& path, AssetRegistry& registry) const {
        json root;
        if (!ReadJson(path, root)) {
            return false;
        }

        if (!root.contains("skies") || !root["skies"].is_array()) {
            return false;
        }

        for (const json& node : root["skies"]) {
            if (!node.is_object()) {
                continue;
            }

            auto descriptor = std::make_unique<SkyAssetDescriptor>();
            descriptor->id.value = node.value("id", "");
            descriptor->type = ParseAssetType(node.value("type", "Sky"));
            descriptor->version = node.value("version", 1u);
            descriptor->meshAssetId = node.value("meshAssetId", "");
            descriptor->textureAssetId = node.value("textureAssetId", "");
            registry.RegisterDescriptor(std::move(descriptor));
        }

        return true;
    }

    bool AssetJsonLoader::LoadTextureDescriptors(const std::string& path, AssetRegistry& registry) const {
        json root;
        if (!ReadJson(path, root)) {
            return false;
        }

        if (!root.contains("textures") || !root["textures"].is_array()) {
            return false;
        }

        for (const json& node : root["textures"]) {
            if (!node.is_object()) {
                continue;
            }

            auto descriptor = std::make_unique<TextureAssetDescriptor>();
            descriptor->id.value = node.value("id", "");
            descriptor->type = ParseAssetType(node.value("type", "Texture"));
            descriptor->sourcePath = node.value("sourcePath", "");
            descriptor->version = node.value("version", 1u);
            registry.RegisterDescriptor(std::move(descriptor));
        }

        return true;
    }

    bool AssetJsonLoader::LoadVfxDescriptors(const std::string& path, AssetRegistry& registry) const {
        json root;
        if (!ReadJson(path, root)) {
            return false;
        }

        if (!root.contains("vfx") || !root["vfx"].is_array()) {
            return false;
        }

        for (const json& node : root["vfx"]) {
            if (!node.is_object()) {
                continue;
            }

            auto descriptor = std::make_unique<VfxAssetDescriptor>();
            descriptor->id.value = node.value("id", "");
            descriptor->type = ParseAssetType(node.value("type", "VfxEffect"));
            descriptor->sourcePath = node.value("sourcePath", "");
            descriptor->version = node.value("version", 1u);
            descriptor->preload = node.value("preload", descriptor->preload);
            descriptor->loopByDefault = node.value("loopByDefault", descriptor->loopByDefault);
            descriptor->defaultScale = node.value("defaultScale", descriptor->defaultScale);
            registry.RegisterDescriptor(std::move(descriptor));
        }

        return true;
    }

} // namespace HIKARI
