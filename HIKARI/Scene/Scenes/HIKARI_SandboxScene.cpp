#include "HIKARI_SandboxScene.h"

#include "HIKARI_Input.h"
#include "Vfx/Runtime/HIKARI_VfxSystem.h"

namespace HIKARI {

    SandboxScene::SandboxScene(SceneCatalog& sceneCatalog, std::string sceneId)
        : DocumentSceneBase(sceneCatalog, std::move(sceneId)) {
    }

    SandboxScene::~SandboxScene() = default;

    void SandboxScene::OnEnter()
    {
        DocumentSceneBase::OnEnter();
       /* VFX::LoadEffect("sword");*/
    }

    void SandboxScene::Update(float dt)
    {
        DocumentSceneBase::Update(dt);

    /*    if (HINPUT::IsPressed("PlayTestVfx")) {
            VFX::Play("sword");
        }*/
    }

    void SandboxScene::Render()
    {
        DocumentSceneBase::Render();
    }

} // namespace HIKARI
