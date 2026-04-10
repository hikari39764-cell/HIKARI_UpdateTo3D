#include "HIKARI_EngineApp.h"
#include <memory>
#include "Scene/Scenes/HIKARI_SandboxScene.h"

namespace HIKARI {

    bool EngineApp::Initialize() {
        sceneManager_.ChangeScene(std::make_unique<SandboxScene>());
        return true;
    }

    void EngineApp::Update(float dt) {
        sceneManager_.Update(dt);
    }

    void EngineApp::Render() {
        sceneManager_.Render();
    }

    void EngineApp::RenderImGui() {
        sceneManager_.RenderImGui();
    }

    void EngineApp::Shutdown() {
        sceneManager_.ChangeScene(nullptr);
        sceneManager_.Update(0.0f);
    }

} // namespace HIKARI
