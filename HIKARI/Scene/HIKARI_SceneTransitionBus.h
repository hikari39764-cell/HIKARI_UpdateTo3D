#pragma once

#include <optional>
#include <string>

namespace HIKARI {

    class SceneCatalog;
    class SceneFactory;
    class SceneManager;

    struct SceneTransitionRequest {
        std::string targetSceneId{};
        std::string targetSpawnPointId{};
        std::string transitionProfileId{ "DefaultFade" };
        bool useTransition = true;
        bool preserveGameplayState = false;
    };

    class SceneTransitionBus {
    public:
        SceneTransitionBus(SceneManager& sceneManager, const SceneCatalog& sceneCatalog, const SceneFactory& sceneFactory);

        bool RequestTransition(const SceneTransitionRequest& request);
        void Update();
        bool IsTransitioning() const;

    private:
        SceneManager& sceneManager_;
        const SceneCatalog& sceneCatalog_;
        const SceneFactory& sceneFactory_;
        std::optional<SceneTransitionRequest> pendingRequest_{};
        bool transitionActive_ = false;
    };

} // namespace HIKARI
