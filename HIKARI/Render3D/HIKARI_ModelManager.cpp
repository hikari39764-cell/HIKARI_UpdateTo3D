#include "HIKARI_ModelManager.h"

namespace HIKARI {

    ModelAsset* ModelManager::RegisterAsset(const std::string& name, const std::string& sourcePath) {
        if (auto found = FindAsset(name)) {
            found->SetSourcePath(sourcePath);
            return found;
        }

        auto asset = std::make_unique<ModelAsset>();
        asset->SetName(name);
        asset->SetSourcePath(sourcePath);
        ModelAsset* ptr = asset.get();
        assets_.push_back(std::move(asset));
        nameToAsset_[name] = ptr;
        return ptr;
    }

    ModelAsset* ModelManager::FindAsset(const std::string& name) {
        auto it = nameToAsset_.find(name);
        if (it == nameToAsset_.end()) {
            return nullptr;
        }
        return it->second;
    }

    const ModelAsset* ModelManager::FindAsset(const std::string& name) const {
        auto it = nameToAsset_.find(name);
        if (it == nameToAsset_.end()) {
            return nullptr;
        }
        return it->second;
    }

    const std::vector<std::unique_ptr<ModelAsset>>& ModelManager::GetAssets() const {
        return assets_;
    }

    bool ModelManager::MarkLoaded(const std::string& name) {
        if (auto* asset = FindAsset(name)) {
            asset->SetState(ModelAsset::State::Loaded);
            return true;
        }
        return false;
    }

    bool ModelManager::MarkFailed(const std::string& name) {
        if (auto* asset = FindAsset(name)) {
            asset->SetState(ModelAsset::State::Failed);
            return true;
        }
        return false;
    }

} // namespace HIKARI
