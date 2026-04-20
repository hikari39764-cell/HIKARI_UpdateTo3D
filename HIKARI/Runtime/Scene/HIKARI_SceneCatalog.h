#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace HIKARI {

    enum class SceneLifetimePolicy {
        ReloadOnEnter,
        KeepAlive,
    };

    struct SceneCatalogEntry {
        std::string sceneId{};
        std::string sceneType{};
        std::string documentPath{};
        bool startupAllowed = true;
        std::string displayName{};
        SceneLifetimePolicy lifetimePolicy = SceneLifetimePolicy::ReloadOnEnter;
    };

    class SceneCatalog {
    public:
        void Register(SceneCatalogEntry entry);
        const SceneCatalogEntry* Find(const std::string& sceneId) const;
        std::vector<std::string> GetSceneIds() const;

    private:
        std::unordered_map<std::string, SceneCatalogEntry> entries_{};
    };

} // namespace HIKARI
