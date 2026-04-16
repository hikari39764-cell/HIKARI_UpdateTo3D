#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace HIKARI {

    class SceneCatalog;
    class SceneFactory;
    class SceneManager;
    class SceneInstanceCache;

    struct SceneTransitionRequest {
        std::string targetSceneId{};
        std::string targetSpawnPointId{};
        std::string transitionProfileId{ "DefaultFade" };
        bool useTransition = true;
        bool preserveGameplayState = false;
    };

    class SceneTransitionBus {
    public:
        enum class TransitionState {
            Idle,
            TransitionOut,
            SwitchingScene,
            TransitionIn,
        };

        SceneTransitionBus(SceneManager& sceneManager, const SceneCatalog& sceneCatalog, const SceneFactory& sceneFactory, SceneInstanceCache& sceneCache);

        bool RequestTransition(const SceneTransitionRequest& request);
        void Update(float dt);
        bool IsTransitioning() const;
        TransitionState GetState() const;
        bool WasLastSceneLoadedFromCache() const;
        size_t GetCachedSceneCount() const;
        std::vector<std::string> GetCachedSceneIds() const;

    private:
        SceneManager& sceneManager_;
        const SceneCatalog& sceneCatalog_;
        const SceneFactory& sceneFactory_;
        SceneInstanceCache& sceneCache_;
        std::optional<SceneTransitionRequest> pendingRequest_{};
        TransitionState state_ = TransitionState::Idle;
        float timer_ = 0.0f;
        float transitionOutDuration_ = 0.2f;
        float transitionInDuration_ = 0.2f;
        bool lastSceneLoadedFromCache_ = false;
    };

} // namespace HIKARI
