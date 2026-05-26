#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace HIKARI {

    struct TransitionVisualState {
        bool active = false;
        std::string profileId{};
        float progress = 0.0f;
        bool isTransitionIn = false;
        float outDuration = 0.0f; // 追加: outDuration メンバー
        float inDuration = 0.0f;  // 追加: inDuration メンバー（必要に応じて）
    };

    class SceneCatalog;
    class SceneFactory;
    class SceneManager;
    class SceneInstanceCache;

    struct SceneTransitionRequest {
        std::string targetSceneAssetGuid{};
        std::string targetSpawnPointId{};
        std::string transitionProfileId{ "Default" };
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
        TransitionVisualState GetVisualState() const;

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
        std::string activeTransitionProfileId_{ "noise_wipe" };
    };

} // namespace HIKARI
