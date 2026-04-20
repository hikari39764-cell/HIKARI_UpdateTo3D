#include "Runtime/Scene/HIKARI_SceneInstanceCache.h"

#include "Runtime/Scene/HIKARI_IScene.h"

namespace HIKARI {

    IScene* SceneInstanceCache::Find(std::string_view sceneId) const {
        const auto it = cachedScenes_.find(std::string(sceneId));
        if (it == cachedScenes_.end()) {
            return nullptr;
        }
        return it->second.get();
    }

    std::unique_ptr<IScene> SceneInstanceCache::Take(std::string_view sceneId) {
        const auto it = cachedScenes_.find(std::string(sceneId));
        if (it == cachedScenes_.end()) {
            return nullptr;
        }

        std::unique_ptr<IScene> scene = std::move(it->second);
        cachedScenes_.erase(it);
        return scene;
    }

    void SceneInstanceCache::Store(std::string sceneId, std::unique_ptr<IScene> scene) {
        if (sceneId.empty() || !scene) {
            return;
        }
        cachedScenes_[std::move(sceneId)] = std::move(scene);
    }

    void SceneInstanceCache::Clear(std::string_view sceneId) {
        cachedScenes_.erase(std::string(sceneId));
    }

    void SceneInstanceCache::ClearAll() {
        cachedScenes_.clear();
    }

    size_t SceneInstanceCache::GetCachedCount() const {
        return cachedScenes_.size();
    }

    std::vector<std::string> SceneInstanceCache::GetCachedSceneIds() const {
        std::vector<std::string> ids{};
        ids.reserve(cachedScenes_.size());
        for (const auto& [sceneId, _] : cachedScenes_) {
            ids.push_back(sceneId);
        }
        return ids;
    }

} // namespace HIKARI
