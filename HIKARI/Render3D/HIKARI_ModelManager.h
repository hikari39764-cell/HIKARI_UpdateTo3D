#pragma once
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>
#include "HIKARI_ModelAsset.h"

namespace HIKARI {

    class ModelManager {
    public:
        ModelAsset* RegisterAsset(const std::string& name, const std::string& sourcePath);
        ModelAsset* FindAsset(const std::string& name);
        const ModelAsset* FindAsset(const std::string& name) const;

        const std::vector<std::unique_ptr<ModelAsset>>& GetAssets() const;

        bool MarkLoaded(const std::string& name);
        bool MarkFailed(const std::string& name);

    private:
        std::vector<std::unique_ptr<ModelAsset>> assets_;
        std::unordered_map<std::string, ModelAsset*> nameToAsset_;
    };

} // namespace HIKARI
