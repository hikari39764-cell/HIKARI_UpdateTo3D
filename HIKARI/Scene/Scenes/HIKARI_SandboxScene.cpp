#include "HIKARI_SandboxScene.h"

#include "HIKARI_Input.h"
#include "Vfx/HIKARI_VfxSystem.h"

namespace HIKARI {

    SandboxScene::SandboxScene(SceneCatalog& sceneCatalog, std::string sceneId)
        : DocumentSceneBase(sceneCatalog, std::move(sceneId)) {
    }

    SandboxScene::~SandboxScene() = default;

    void SandboxScene::OnEnter()
    {
        DocumentSceneBase::OnEnter();
        VFX::LoadEffect("Laser01");
    }

    void SandboxScene::Update(float dt)
    {
        DocumentSceneBase::Update(dt);

        if (HINPUT::IsPressed("PlayTestVfx")) {
            VFX::Play("Laser01");
        }
    }

    void SandboxScene::Render()
    {
        DocumentSceneBase::Render();
    }

} // namespace HIKARI
