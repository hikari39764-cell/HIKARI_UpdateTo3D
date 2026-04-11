#include "HIKARI_SkyManager.h"

namespace HIKARI {

    void SkyManager::RegisterAsset(const SkyAsset& asset) {
        if (asset.name.empty()) {
            return;
        }
        assets_[asset.name] = asset;
    }

    const SkyAsset* SkyManager::FindAsset(const std::string& name) const {
        const auto it = assets_.find(name);
        if (it == assets_.end()) {
            return nullptr;
        }
        return &it->second;
    }

} // namespace HIKARI
