#include "HIKARI_SceneManager.h"

namespace HIKARI {

    SceneManager::~SceneManager() = default;

    void SceneManager::ChangeScene(std::unique_ptr<IScene> next) {
        pending_ = std::move(next);
    }

    void SceneManager::Update(float dt) {
        CommitPendingScene();
        if (current_) {
            current_->Update(dt);
        }
    }

    void SceneManager::Render() {
        if (current_) {
            current_->Render();
        }
    }

    void SceneManager::RenderImGui() {
        if (current_) {
            current_->RenderImGui();
        }
    }

    IScene* SceneManager::GetCurrentScene() {
        return current_.get();
    }

    const IScene* SceneManager::GetCurrentScene() const {
        return current_.get();
    }

    void SceneManager::CommitPendingScene() {
        if (!pending_) {
            return;
        }

        if (current_) {
            current_->OnExit();
        }

        current_ = std::move(pending_);

        if (current_) {
            current_->OnEnter();
        }
    }

} // namespace HIKARI
