#include "HIKARI_SceneTransitionBus.h"

#include <algorithm>

#include "HIKARI_RuntimeSceneContext.h"
#include "HIKARI_SceneCatalog.h"
#include "HIKARI_SceneFactory.h"
#include "HIKARI_SceneInstanceCache.h"
#include "HIKARI_IScene.h"
#include "HIKARI_SceneManager.h"

namespace HIKARI {

    SceneTransitionBus::SceneTransitionBus(SceneManager& sceneManager, const SceneCatalog& sceneCatalog, const SceneFactory& sceneFactory, SceneInstanceCache& sceneCache)
        : sceneManager_(sceneManager), sceneCatalog_(sceneCatalog), sceneFactory_(sceneFactory), sceneCache_(sceneCache) {
    }

    bool SceneTransitionBus::RequestTransition(const SceneTransitionRequest& request) {
        if (request.targetSceneId.empty()) {
            return false;
        }
        if (!sceneCatalog_.Find(request.targetSceneId)) {
            return false;
        }

        pendingRequest_ = request;
        state_ = request.useTransition ? TransitionState::TransitionOut : TransitionState::SwitchingScene;
        timer_ = 0.0f;
        return true;
    }

    void SceneTransitionBus::Update(float dt) {
        if (!pendingRequest_.has_value()) {
            state_ = TransitionState::Idle;
            timer_ = 0.0f;
            return;
        }

        switch (state_) {
        case TransitionState::Idle:
            state_ = pendingRequest_->useTransition ? TransitionState::TransitionOut : TransitionState::SwitchingScene;
            timer_ = 0.0f;
            break;

        case TransitionState::TransitionOut:
            timer_ += (std::max)(0.0f, dt);
            if (timer_ >= transitionOutDuration_) {
                state_ = TransitionState::SwitchingScene;
                timer_ = 0.0f;
            }
            break;

        case TransitionState::SwitchingScene: {
            RuntimeSceneContext::SetPendingSceneEntry(pendingRequest_->targetSceneId, pendingRequest_->targetSpawnPointId);
            IScene* currentScene = sceneManager_.GetCurrentScene();
            bool callOnExitCurrent = true;
            if (currentScene) {
                const SceneCatalogEntry* currentEntry = sceneCatalog_.Find(currentScene->GetSceneId());
                if (currentEntry && currentEntry->lifetimePolicy == SceneLifetimePolicy::KeepAlive) {
                    std::unique_ptr<IScene> cachedCurrent = sceneManager_.TakeCurrentScene();
                    sceneCache_.Store(currentEntry->sceneId, std::move(cachedCurrent));
                    callOnExitCurrent = false;
                }
            }

            std::unique_ptr<IScene> nextScene{};
            bool callOnEnterNext = true;
            const SceneCatalogEntry* targetEntry = sceneCatalog_.Find(pendingRequest_->targetSceneId);
            if (targetEntry && targetEntry->lifetimePolicy == SceneLifetimePolicy::KeepAlive) {
                nextScene = sceneCache_.Take(targetEntry->sceneId);
                if (nextScene) {
                    callOnEnterNext = false;
                    lastSceneLoadedFromCache_ = true;
                }
            }

            if (!nextScene) {
                nextScene = sceneFactory_.CreateScene(pendingRequest_->targetSceneId);
                lastSceneLoadedFromCache_ = false;
            }
            if (nextScene) {
                sceneManager_.ChangeScene(std::move(nextScene), callOnEnterNext, callOnExitCurrent);
            }

            if (pendingRequest_->useTransition) {
                state_ = TransitionState::TransitionIn;
                timer_ = 0.0f;
            } else {
                state_ = TransitionState::Idle;
                pendingRequest_.reset();
            }
            break;
        }

        case TransitionState::TransitionIn:
            timer_ += (std::max)(0.0f, dt);
            if (timer_ >= transitionInDuration_) {
                state_ = TransitionState::Idle;
                timer_ = 0.0f;
                pendingRequest_.reset();
            }
            break;
        }
    }

    bool SceneTransitionBus::IsTransitioning() const {
        return state_ != TransitionState::Idle || pendingRequest_.has_value();
    }

    SceneTransitionBus::TransitionState SceneTransitionBus::GetState() const {
        return state_;
    }

    bool SceneTransitionBus::WasLastSceneLoadedFromCache() const {
        return lastSceneLoadedFromCache_;
    }

    size_t SceneTransitionBus::GetCachedSceneCount() const {
        return sceneCache_.GetCachedCount();
    }

    std::vector<std::string> SceneTransitionBus::GetCachedSceneIds() const {
        return sceneCache_.GetCachedSceneIds();
    }

} // namespace HIKARI
