#pragma once

#include <string>
#include <unordered_set>

namespace HIKARI {

    class AssetRegistry;
    class ComponentRegistry;
    class ModelManager;
    class SkyManager;
    class World;
    struct SceneDocument;

    struct SceneDependencySet {
        std::unordered_set<std::string> modelAssetIds{};
        std::unordered_set<std::string> skyAssetIds{};
        std::unordered_set<std::string> textureAssetIds{};
        std::unordered_set<std::string> materialAssetIds{};
    };

    class SceneRuntimeBuilder {
    public:
        SceneDependencySet CollectDependencies(const SceneDocument& document) const;
        bool PreloadDependencies(
            const SceneDependencySet& dependencies,
            const AssetRegistry& assetRegistry,
            ModelManager& modelManager,
            SkyManager& skyManager) const;

        bool BuildWorldFromDocument(
            const SceneDocument& document,
            World& world,
            const AssetRegistry& assetRegistry,
            const ComponentRegistry& componentRegistry,
            ModelManager& modelManager,
            SkyManager& skyManager) const;
    };

} // namespace HIKARI
