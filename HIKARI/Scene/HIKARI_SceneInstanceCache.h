#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace HIKARI {

    class IScene;

    class SceneInstanceCache {
    public:
        IScene* Find(std::string_view sceneId) const;
        std::unique_ptr<IScene> Take(std::string_view sceneId);
        void Store(std::string sceneId, std::unique_ptr<IScene> scene);
        void Clear(std::string_view sceneId);
        void ClearAll();

        size_t GetCachedCount() const;
        std::vector<std::string> GetCachedSceneIds() const;

    private:
        std::unordered_map<std::string, std::unique_ptr<IScene>> cachedScenes_{};
    };

} // namespace HIKARI
