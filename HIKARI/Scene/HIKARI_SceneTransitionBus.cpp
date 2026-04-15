#include "HIKARI_SceneTransitionBus.h"

#include <algorithm>

#include "HIKARI_RuntimeSceneContext.h"
#include "HIKARI_SceneCatalog.h"
#include "HIKARI_SceneFactory.h"
#include "HIKARI_IScene.h"
#include "HIKARI_SceneManager.h"

namespace HIKARI {

    SceneTransitionBus::SceneTransitionBus(SceneManager& sceneManager, const SceneCatalog& sceneCatalog, const SceneFactory& sceneFactory)
        : sceneManager_(sceneManager), sceneCatalog_(sceneCatalog), sceneFactory_(sceneFactory) {
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
            std::unique_ptr<IScene> nextScene = sceneFactory_.CreateScene(pendingRequest_->targetSceneId);
            if (nextScene) {
                sceneManager_.ChangeScene(std::move(nextScene));
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

} // namespace HIKARI
