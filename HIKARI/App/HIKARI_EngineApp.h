#pragma once
#include "Scene/HIKARI_SceneManager.h"

namespace HIKARI {

    class EngineApp {
    public:
        bool Initialize();
        void Update(float dt);
        void Render();
        void RenderImGui();
        void Shutdown();

    private:
        SceneManager sceneManager_{};
    };

} // namespace HIKARI
