#include "HIKARI_DocumentSceneBase.h"

#include <filesystem>
#include <numbers>

#include "HIKARI_3D.h"
#include "Render3D/HIKARI_LightDebugDraw.h"
#include "Render3D/HIKARI_MeshRenderer.h"
#include "Render3D/HIKARI_SkyRenderer.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/Components/HIKARI_UIButtonSceneTransitionComponent.h"

namespace HIKARI {

    DocumentSceneBase::DocumentSceneBase(SceneCatalog& sceneCatalog, std::string sceneId)
        : sceneCatalog_(sceneCatalog), sceneId_(std::move(sceneId)) {
    }

    void DocumentSceneBase::OnEnter() {
        camera_.SetPerspective(60.0f * std::numbers::pi_v<float> / 180.0f, static_cast<float>(kScreenW) / static_cast<float>(kScreenH), 0.1f, 100.0f);
        debugCamera_.Reset({ 0.0f, 2.0f, -6.0f }, 0.0f, 0.0f);

        RegisterDefaultComponentTypes();
        RegisterDefaultSceneCatalogEntries();

        ReloadAssets();
        ReloadSceneDocument();
        RebuildRuntimeWorld();
    }

    void DocumentSceneBase::OnExit() {
    }

    void DocumentSceneBase::Update(float dt) {
        if (UseDebugCamera()) {
            debugCamera_.Update(dt, camera_);
        }
        world_.Update(dt);
    }

    void DocumentSceneBase::Render() {
        RENDERER3D::Reset();
        MESHRENDERER::Reset();
        SKYRENDERER::Reset();

        if (DrawDebugHelpers()) {
            RENDERER3D::DEBUG::Grid3D grid{};
            grid.halfCount = 10;
            grid.spacing = 1.0f;
            RENDERER3D::DEBUG::SubmitGrid3D(grid);

            RENDERER3D::DEBUG::Axis3D axis{};
            axis.length = 2.5f;
            RENDERER3D::DEBUG::SubmitAxis3D(axis);
        }

        world_.Render();

        for (const auto& object : world_.GetObjects()) {
            if (const ModelComponent* model = object->GetComponent<ModelComponent>()) {
                if (!model->IsVisible()) {
                    continue;
                }

                const ModelAsset* asset = model->GetAsset();
                if (asset && asset->GetState() == ModelAsset::State::Loaded && asset->GetMesh() && asset->GetMesh()->IsValid()) {
                    MESHRENDERER::SubmitStaticMesh(*asset, object->Transform());
                } else {
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
        if (!UseEnvironmentLighting()) {
            activeEnvironment.directional.intensity = 0.0f;
            activeEnvironment.ambient.intensity = 0.0f;
            activeEnvironment.specularIntensity = 0.0f;
            for (PointLight& pointLight : activeEnvironment.pointLights) {
                pointLight.intensity = 0.0f;
            }
        }

        SKYRENDERER::Render(camera_, activeEnvironment.sky, modelManager_, skyManager_);
        if (DrawDebugHelpers()) {
            LIGHTDEBUGDRAW::SubmitDirectionalLightArrow(activeEnvironment.directional.direction, activeEnvironment);
            LIGHTDEBUGDRAW::SubmitPointLightDebug(activeEnvironment);
        }

        MESHRENDERER::RenderAll(camera_, activeEnvironment);
        RENDERER3D::RenderAll(camera_, static_cast<float>(kScreenW), static_cast<float>(kScreenH));
    }

    void DocumentSceneBase::RenderImGui() {
        world_.RenderImGui();
    }

    const std::string& DocumentSceneBase::GetSceneId() const {
        return sceneId_;
    }

    bool DocumentSceneBase::ReloadAssets() {
        assetRegistry_.Clear();
        const bool okModels = assetJsonLoader_.LoadModelDescriptors("Data/assets_models.json", assetRegistry_);
        const bool okSkies = assetJsonLoader_.LoadSkyDescriptors("Data/assets_skies.json", assetRegistry_);
        const bool okTextures = assetJsonLoader_.LoadTextureDescriptors("Data/assets_textures.json", assetRegistry_);
        return okModels && okSkies && okTextures;
    }

    bool DocumentSceneBase::ReloadSceneDocument() {
        const SceneCatalogEntry* entry = sceneCatalog_.Find(sceneId_);
        scenePath_ = (entry && !entry->documentPath.empty()) ? entry->documentPath : "Data/scenes/scene_sandbox.json";

        if (!sceneSerializer_.LoadFromFile(scenePath_, sceneDocument_)) {
            return false;
        }

        environment_ = sceneDocument_.environment;
        return true;
    }

    bool DocumentSceneBase::RebuildRuntimeWorld() {
        const SceneDependencySet deps = runtimeBuilder_.CollectDependencies(sceneDocument_);
        runtimeBuilder_.PreloadDependencies(deps, assetRegistry_, modelManager_, skyManager_);
        const bool built = runtimeBuilder_.BuildWorldFromDocument(sceneDocument_, world_, assetRegistry_, componentRegistry_, modelManager_, skyManager_);

        environment_ = sceneDocument_.environment;
        environment_.directional.direction = MATH::Normalize(environment_.directional.direction);
        if (environment_.pointLights.empty()) {
            environment_.pointLights.push_back(PointLight{});
        }

        return built;
    }

    void DocumentSceneBase::RegisterDefaultComponentTypes() {
        if (!componentRegistry_.Find("ModelComponent")) {
            componentRegistry_.Register(ComponentTypeInfo{
                "ModelComponent",
                []() -> std::unique_ptr<IComponent> { return std::make_unique<ModelComponent>(); }
            });
        }

        if (!componentRegistry_.Find("UIButtonSceneTransitionComponent")) {
            componentRegistry_.Register(ComponentTypeInfo{
                "UIButtonSceneTransitionComponent",
                []() -> std::unique_ptr<IComponent> { return std::make_unique<UIButtonSceneTransitionComponent>(); }
            });
        }
    }

    void DocumentSceneBase::RegisterDefaultSceneCatalogEntries() {
        sceneCatalog_.Register(SceneCatalogEntry{ "Sandbox", "SandboxScene", "Data/scenes/scene_sandbox.json", true, "Sandbox" });
        sceneCatalog_.Register(SceneCatalogEntry{ "Empty", "GameDocumentScene", "Data/scenes/scene_empty.json", true, "Empty" });
        sceneCatalog_.Register(SceneCatalogEntry{ "Title", "TitleScene", "Data/scenes/scene_title.json", true, "Title" });

        std::error_code ec{};
        const std::filesystem::path sceneRoot{ "Data/scenes" };
        if (!std::filesystem::exists(sceneRoot, ec) || ec) {
            return;
        }

        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(sceneRoot, ec)) {
            if (ec || !entry.is_regular_file()) {
                continue;
            }
            const std::filesystem::path& path = entry.path();
            if (path.extension() != ".json") {
                continue;
            }

            std::string stem = path.stem().string();
            if (stem.rfind("scene_", 0) == 0) {
                stem.erase(0, 6);
            }
            if (stem.empty()) {
                continue;
            }

            if (stem == "sandbox" || stem == "empty" || stem == "title") {
                continue;
            }

            sceneCatalog_.Register(SceneCatalogEntry{ stem, "GameDocumentScene", path.generic_string(), true, stem });
        }
    }

    bool DocumentSceneBase::UseDebugCamera() const {
        return true;
    }

    bool DocumentSceneBase::DrawDebugHelpers() const {
        return false;
    }

    bool DocumentSceneBase::UseEnvironmentLighting() const {
        return environmentLightingEnabled_;
    }

} // namespace HIKARI
