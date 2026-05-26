#include "HIKARI_EngineApp.h"

#include <memory>
#include <string>

#include "HIKARI_Services.h"
#include "Scene/Scenes/HIKARI_GameDocumentScene.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

namespace HIKARI {

    EngineApp::EngineApp()
        : sceneFactory_(sceneCatalog_),
        sceneTransitionBus_(sceneManager_, sceneCatalog_, sceneFactory_, sceneInstanceCache_) {
    }

    bool EngineApp::Initialize() {
        HIKARI_LOG_INFO("EngineApp initialization started.");

        // 起動時は SceneCatalog ではなく、DocumentSceneBase が Scene Asset GUID を解決する。
        std::unique_ptr<IScene> initialScene = std::make_unique<GameDocumentScene>(sceneCatalog_, "StartupSceneAsset");

        RuntimeSceneContext::SetTransitionBus(&sceneTransitionBus_);
        sceneManager_.ChangeScene(std::move(initialScene));
        HIKARI_LOG_INFO("Initial document scene created.");
        HIKARI_LOG_INFO("EngineApp initialization completed.");
        return true;
    }

    void EngineApp::Update(float dt) {
        sceneManager_.Update(dt);
        sceneTransitionBus_.Update(dt);
        const TransitionVisualState visualState = sceneTransitionBus_.GetVisualState();
        if (visualState.active) {
            POST::PostSystem::SetTransitionState(visualState);
        } else {
            POST::PostSystem::ClearTransitionState();
        }
    }

    void EngineApp::Render() {
        sceneManager_.Render();
    }

    void EngineApp::RenderImGui() {
#if defined(_DEBUG)
        sceneManager_.RenderImGui();

        if (!SERVICES::IsEditorUIEnabled()) {
            return;
        }

        if (auto* docScene = dynamic_cast<DocumentSceneBase*>(sceneManager_.GetCurrentScene())) {
            documentSceneEditorController_.Draw(*docScene);
        }
#endif
    }

    void EngineApp::Shutdown() {
        RuntimeSceneContext::SetTransitionBus(nullptr);
        sceneInstanceCache_.ClearAll();
        sceneManager_.ChangeScene(nullptr);
        sceneManager_.Update(0.0f);
    }

} // namespace HIKARI
