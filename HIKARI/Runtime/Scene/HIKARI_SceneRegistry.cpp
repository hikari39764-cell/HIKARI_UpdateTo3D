#include "HIKARI_SceneRegistry.h"

namespace HIKARI {

    void SceneRegistry::RegisterScene(std::string sceneId, std::string path) {
        if (sceneId.empty() || path.empty()) {
            return;
        }
        entries_[std::move(sceneId)] = std::move(path);
    }

    const std::string* SceneRegistry::FindPath(const std::string& sceneId) const {
        const auto it = entries_.find(sceneId);
        if (it == entries_.end()) {
            return nullptr;
        }
        return &it->second;
    }

    std::vector<std::string> SceneRegistry::GetSceneIds() const {
        std::vector<std::string> ids{};
        ids.reserve(entries_.size());
        for (const auto& [id, _] : entries_) {
            ids.push_back(id);
        }
        return ids;
    }

} // namespace HIKARI
