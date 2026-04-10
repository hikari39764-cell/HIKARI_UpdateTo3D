#include "HIKARI_SandboxScene.h"
#include <numbers>
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "HIKARI/HIKARI_3D.h"

namespace HIKARI {

    void SandboxScene::OnEnter() {
        camera_.SetPerspective(60.0f * std::numbers::pi_v<float> / 180.0f, static_cast<float>(kScreenW) / static_cast<float>(kScreenH), 0.1f, 100.0f);
        camera_.SetLookAt({ 0.0f, 2.0f, -6.0f }, { 0.0f, 0.0f, 0.0f });

        modelManager_.RegisterAsset("TestCube", "Assets/Models/TestCube.obj");
        modelManager_.RegisterAsset("TestCharacter", "Assets/Models/TestCharacter.gltf");
        modelManager_.RegisterAsset("TestStage", "Assets/Models/TestStage.gltf");

        GameObject* debugGrid = world_.CreateObject("DebugGrid");
        (void)debugGrid;
        GameObject* axis = world_.CreateObject("Axis");
        (void)axis;

        GameObject* testCube = world_.CreateObject("TestCube");
        testCube->Transform().position = { 0.0f, 1.0f, 0.0f };
        ModelComponent* modelComponent = testCube->AddComponent<ModelComponent>();
        modelComponent->SetAsset(modelManager_.FindAsset("TestCube"));

        selection_.selectedObject = testCube;
        selection_.selectedAsset = modelManager_.FindAsset("TestCube");
    }

    void SandboxScene::OnExit() {
    }

    void SandboxScene::Update(float dt) {
        spinAngle_ += dt;
        if (selection_.selectedObject) {
            selection_.selectedObject->Transform().rotation = MATH::Quat::FromEulerXYZ(0.0f, spinAngle_, 0.0f);
        }
        world_.Update(dt);
    }

    void SandboxScene::Render() {
        RENDERER3D::Reset();

        RENDERER3D::DEBUG::Grid3D grid{};
        grid.halfCount = 10;
        grid.spacing = 1.0f;
        RENDERER3D::DEBUG::SubmitGrid3D(grid);

        RENDERER3D::DEBUG::Axis3D axis{};
        axis.length = 2.5f;
        RENDERER3D::DEBUG::SubmitAxis3D(axis);

        world_.Render();

        for (const auto& object : world_.GetObjects()) {
            if (const ModelComponent* model = object->GetComponent<ModelComponent>()) {
                if (!model->IsVisible()) {
                    continue;
                }

                RENDERER3D::WireCube cube{};
                cube.transform = object->Transform();
                cube.size = 1.0f;
                cube.rgba = 0x66CCFFFF;
                RENDERER3D::SubmitWireCube(cube);
            }
        }

        RENDERER3D::RenderAll(camera_, static_cast<float>(kScreenW), static_cast<float>(kScreenH));
    }

    void SandboxScene::RenderImGui() {
        hierarchyPanel_.Draw(world_, selection_);
        inspectorPanel_.Draw(selection_);
        assetBrowserPanel_.Draw(modelManager_, selection_);
        statsPanel_.Draw(GetSceneName(), world_, modelManager_, selection_, camera_);
    }

} // namespace HIKARI
