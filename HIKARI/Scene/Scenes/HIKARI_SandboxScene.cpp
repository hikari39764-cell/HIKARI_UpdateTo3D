#include "HIKARI_SandboxScene.h"

#include <numbers>

#include "HIKARI_3D.h"
#include "Render3D/HIKARI_LightDebugDraw.h"
#include "Render3D/HIKARI_MeshRenderer.h"
#include "Render3D/HIKARI_SkyRenderer.h"
#include "Scene/Components/HIKARI_ModelComponent.h"

namespace HIKARI {

    void SandboxScene::OnEnter() {
        camera_.SetPerspective(60.0f * std::numbers::pi_v<float> / 180.0f, static_cast<float>(kScreenW) / static_cast<float>(kScreenH), 0.1f, 100.0f);
        debugCamera_.Reset({ 0.0f, 2.0f, -6.0f }, 0.0f, 0.0f);

        modelManager_.RegisterAsset("Block", "cube.obj");
        modelManager_.RegisterAsset("TestCube", "builtin:cube");
        modelManager_.RegisterAsset("SkySphere", "cube.obj");
        modelManager_.LoadAllRegisteredAssets();

        GameObject* debugGrid = world_.CreateObject("DebugGrid");
        (void)debugGrid;
        GameObject* axis = world_.CreateObject("Axis");
        (void)axis;

        GameObject* block = world_.CreateObject("Block");
        block->Transform().position = { 0.0f, 0.5f, 0.0f };
        ModelComponent* blockModelComponent = block->AddComponent<ModelComponent>();
        blockModelComponent->SetAsset(modelManager_.FindAsset("Block"));

        GameObject* testCube = world_.CreateObject("TestCube");
        testCube->Transform().position = { 2.0f, 1.0f, 0.0f };
        ModelComponent* cubeModelComponent = testCube->AddComponent<ModelComponent>();
        cubeModelComponent->SetAsset(modelManager_.FindAsset("TestCube"));

        selection_.selectedObject = block;
        selection_.selectedAsset = modelManager_.FindAsset("Block");
        environment_.directional.direction = MATH::Normalize(environment_.directional.direction);
        if (environment_.pointLights.empty()) {
            environment_.pointLights.push_back(PointLight{});
        }
    }

    void SandboxScene::OnExit() {
    }

    void SandboxScene::Update(float dt) {
        debugCamera_.Update(dt, camera_);
        world_.Update(dt);
    }

    void SandboxScene::Render() {
        RENDERER3D::Reset();
        MESHRENDERER::Reset();
        SKYRENDERER::Reset();

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

                const ModelAsset* asset = model->GetAsset();
                if (asset && asset->GetState() == ModelAsset::State::Loaded && asset->GetMesh() && asset->GetMesh()->IsValid()) {
                    MESHRENDERER::SubmitStaticMesh(*asset, object->Transform());
                }
                else {
                    RENDERER3D::WireCube cube{};
                    cube.transform = object->Transform();
                    cube.size = 1.0f;
                    cube.rgba = 0x66CCFFFF;
                    RENDERER3D::SubmitWireCube(cube);
                }
            }
        }

        SceneEnvironment activeEnvironment = environment_;
        activeEnvironment.directional.direction = MATH::Normalize(activeEnvironment.directional.direction);
        if (!environmentLightingEnabled_) {
            activeEnvironment.directional.intensity = 0.0f;
            activeEnvironment.ambient.intensity = 0.0f;
            activeEnvironment.specularIntensity = 0.0f;
            for (PointLight& pointLight : activeEnvironment.pointLights) {
                pointLight.intensity = 0.0f;
            }
        }

        SKYRENDERER::Render(camera_, activeEnvironment.sky, modelManager_);
        LIGHTDEBUGDRAW::SubmitDirectionalLightArrow(activeEnvironment.directional.direction, activeEnvironment);
        LIGHTDEBUGDRAW::SubmitPointLightDebug(activeEnvironment);
        MESHRENDERER::RenderAll(camera_, activeEnvironment);
        RENDERER3D::RenderAll(camera_, static_cast<float>(kScreenW), static_cast<float>(kScreenH));
    }

    void SandboxScene::RenderImGui() {
#if defined(_DEBUG)
        debugMenuBar_.Draw(debugWindowState_, debugCamera_, environmentLightingEnabled_);

        if (debugWindowState_.showHierarchy) {
            hierarchyPanel_.Draw(world_, selection_);
        }
        if (debugWindowState_.showInspector) {
            inspectorPanel_.Draw(selection_);
        }
        if (debugWindowState_.showAssetBrowser) {
            assetBrowserPanel_.Draw(modelManager_, selection_);
        }
        if (debugWindowState_.showStats) {
            statsPanel_.Draw(GetSceneName(), world_, modelManager_, selection_, camera_);
        }
        if (debugWindowState_.showEnvironment) {
            environmentPanel_.Draw(environment_);
        }
        if (debugWindowState_.showDebugCamera) {
            debugCameraPanel_.Draw(debugCamera_);
        }
#endif
    }

} // namespace HIKARI
