#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace HIKARI {

    class SceneRegistry {
    public:
        void RegisterScene(std::string sceneId, std::string path);
        const std::string* FindPath(const std::string& sceneId) const;
        std::vector<std::string> GetSceneIds() const;

    private:
        std::unordered_map<std::string, std::string> entries_{};
    };

} // namespace HIKARI
