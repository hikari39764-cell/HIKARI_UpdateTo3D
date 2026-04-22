#pragma once

#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"
#include "HIKARI_2D.h"
#include <HIKARI.h>
namespace HIKARI {

    class SandboxScene final : public DocumentSceneBase {
    public:
        SandboxScene(SceneCatalog& sceneCatalog, std::string sceneId = "Sandbox");
        ~SandboxScene() override;

        const char* GetSceneName() const override { return "SandboxScene"; }

        void OnEnter() override;
        void Update(float dt) override;
        void Render() override;

    private:
        bool DrawDebugHelpers() const override { return true; }
        HIKARI::SpineActor op;

    };

} // namespace HIKARI