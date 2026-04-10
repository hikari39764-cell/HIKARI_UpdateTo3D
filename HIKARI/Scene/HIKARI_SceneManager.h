#pragma once
#include <memory>
#include "Scene/HIKARI_IScene.h"
namespace HIKARI {

    class SceneManager {
    public:
        void ChangeScene(std::unique_ptr<IScene> next);
        void Update(float dt);
        void Render();
        void RenderImGui();

        IScene* GetCurrentScene();
        const IScene* GetCurrentScene() const;

    private:
        void CommitPendingScene();

    private:
        std::unique_ptr<IScene> current_;
        std::unique_ptr<IScene> pending_;
    };

} // namespace HIKARI
