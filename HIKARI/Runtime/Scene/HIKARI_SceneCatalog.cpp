#include "Runtime/Scene/HIKARI_SceneCatalog.h"

namespace HIKARI {

    void SceneCatalog::Register(SceneCatalogEntry entry) {
        if (entry.sceneId.empty() || entry.sceneType.empty()) {
            return;
        }
        entries_[entry.sceneId] = std::move(entry);
    }

    const SceneCatalogEntry* SceneCatalog::Find(const std::string& sceneId) const {
        const auto it = entries_.find(sceneId);
        if (it == entries_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    std::vector<std::string> SceneCatalog::GetSceneIds() const {
        std::vector<std::string> ids{};
        ids.reserve(entries_.size());
        for (const auto& [sceneId, _] : entries_) {
            ids.push_back(sceneId);
        }
        return ids;
    }

} // namespace HIKARI
