#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "Render3D/HIKARI_ModelAsset.h"

namespace HIKARI {

    class ModelManager {
    public:
        ModelAsset* RegisterAsset(const std::string& name, const std::string& sourcePath);
        ModelAsset* FindAsset(const std::string& name);
        const ModelAsset* FindAsset(const std::string& name) const;

        const std::vector<std::unique_ptr<ModelAsset>>& GetAssets() const;

        bool MarkLoaded(const std::string& name);
        bool MarkFailed(const std::string& name);

        bool LoadAssetNow(const std::string& name);
        bool LoadAllRegisteredAssets();
        bool LoadCpuAssetFromSource(ModelAsset& asset);

        size_t CountLoadedAssets() const;
        size_t CountFailedAssets() const;

    private:
        bool LoadAsObj(ModelAsset& asset, bool buildRuntimeResources);
        bool LoadAsGltf(ModelAsset& asset, bool buildRuntimeResources);
        bool LoadAsHmodel(ModelAsset& asset);
        bool BuildRuntimeResources(ModelAsset& asset);
        bool BuildBuiltinCube(ModelAsset& asset);

    private:
        std::vector<std::unique_ptr<ModelAsset>> assets_;
        std::unordered_map<std::string, ModelAsset*> nameToAsset_;
    };

} // namespace HIKARI
