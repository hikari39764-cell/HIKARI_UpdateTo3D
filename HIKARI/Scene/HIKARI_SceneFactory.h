#pragma once

#include <memory>
#include <string>

namespace HIKARI {

    class IScene;
    class SceneCatalog;

    class SceneFactory {
    public:
        explicit SceneFactory(SceneCatalog& sceneCatalog);

        std::unique_ptr<IScene> CreateScene(const std::string& sceneId) const;

    private:
        SceneCatalog& sceneCatalog_;
    };

} // namespace HIKARI
