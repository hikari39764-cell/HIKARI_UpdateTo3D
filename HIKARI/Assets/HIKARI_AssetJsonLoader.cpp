#include "HIKARI_AssetJsonLoader.h"

#include <fstream>

#include <json.hpp>

#include "HIKARI_AssetRegistry.h"

namespace HIKARI {

    using nlohmann::json;

    namespace {
        AssetType ParseAssetType(const std::string& type) {
            if (type == "Model") return AssetType::Model;
            if (type == "Sky") return AssetType::Sky;
            if (type == "Texture") return AssetType::Texture;
            if (type == "Material") return AssetType::Material;
            if (type == "Animation") return AssetType::Animation;
            if (type == "Particle") return AssetType::Particle;
            if (type == "VfxEffect") return AssetType::VfxEffect;
            if (type == "PostProfile") return AssetType::PostProfile;
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
    }

    bool AssetJsonLoader::LoadModelDescriptors(const std::string& path, AssetRegistry& registry) const {
        json root;
        if (!ReadJson(path, root)) {
            return false;
        }

        if (!root.contains("models") || !root["models"].is_array()) {
            return false;
        }

        for (const json& node : root["models"]) {
            if (!node.is_object()) {
                continue;
            }

            auto descriptor = std::make_unique<ModelAssetDescriptor>();
            descriptor->id.value = node.value("id", "");
            descriptor->type = ParseAssetType(node.value("type", "Model"));
            descriptor->sourcePath = node.value("sourcePath", "");
            descriptor->version = node.value("version", 1u);
            descriptor->forceFlatNormals = node.value("forceFlatNormals", false);
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

    bool AssetJsonLoader::LoadPostProfileDescriptors(const std::string& path, AssetRegistry& registry) const {
        json root;
        if (!ReadJson(path, root)) {
            return false;
        }

        if (!root.contains("postProfiles") || !root["postProfiles"].is_array()) {
            return false;
        }

        for (const json& node : root["postProfiles"]) {
            if (!node.is_object()) {
                continue;
            }

            auto descriptor = std::make_unique<PostProfileAssetDescriptor>();
            descriptor->id.value = node.value("id", "");
            descriptor->type = ParseAssetType(node.value("type", "PostProfile"));
            descriptor->sourcePath = node.value("sourcePath", "");
            descriptor->version = node.value("version", 1u);
            registry.RegisterDescriptor(std::move(descriptor));
        }

        return true;
    }

} // namespace HIKARI
