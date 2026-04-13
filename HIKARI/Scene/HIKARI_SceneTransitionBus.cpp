#include "HIKARI_SceneTransitionBus.h"

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
        transitionActive_ = true;
        return true;
    }

    void SceneTransitionBus::Update() {
        if (!pendingRequest_.has_value()) {
            transitionActive_ = false;
            return;
        }

        // 第一版：立即切场，后续可插入 transition out / in 状态机。
        std::unique_ptr<IScene> nextScene = sceneFactory_.CreateScene(pendingRequest_->targetSceneId);
        if (nextScene) {
            sceneManager_.ChangeScene(std::move(nextScene));
        }

        pendingRequest_.reset();
        transitionActive_ = false;
    }

    bool SceneTransitionBus::IsTransitioning() const {
        return transitionActive_ || pendingRequest_.has_value();
    }

} // namespace HIKARI
