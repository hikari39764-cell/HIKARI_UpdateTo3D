#pragma once
#include <memory>

#include "Editor/HIKARI_DocumentSceneEditorController.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneCatalog.h"
#include "Scene/HIKARI_SceneFactory.h"
#include "Scene/HIKARI_SceneInstanceCache.h"
#include "Scene/HIKARI_SceneManager.h"
#include "Scene/HIKARI_SceneTransitionBus.h"

namespace HIKARI {

    class EngineApp {
    public:
        EngineApp();
        bool Initialize();
        void Update(float dt);
        void Render();
        void RenderImGui();
        void Shutdown();

    private:
        SceneManager sceneManager_{};
        SceneCatalog sceneCatalog_{};
        SceneFactory sceneFactory_;
        SceneInstanceCache sceneInstanceCache_{};
        SceneTransitionBus sceneTransitionBus_;
        DocumentSceneEditorController documentSceneEditorController_{};
    };

} // namespace HIKARI
