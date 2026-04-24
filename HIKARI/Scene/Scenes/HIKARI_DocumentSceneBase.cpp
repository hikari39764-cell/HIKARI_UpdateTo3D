#include "HIKARI_DocumentSceneBase.h"

#include <filesystem>
#include <utility>
#include <numbers>
#include <memory>

#include "HIKARI_3D.h"
#include "Core/HIKARI_TimeService.h"
#include "Render3D/HIKARI_LightDebugDraw.h"
#include "Render3D/Render/HIKARI_ModelRenderer.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Scene/Components/HIKARI_DoorTransitionComponent.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "Scene/Components/HIKARI_SpawnPointComponent.h"
#include "Scene/Components/HIKARI_TriggerVolumeComponent.h"
#include "Scene/Components/HIKARI_UIButtonSceneTransitionComponent.h"
#include "Vfx/Runtime/HIKARI_VfxAsset.h"
#include "Vfx/Runtime/HIKARI_VfxSystem.h"
#include "Vfx/Post/HIKARI_PostSystem.h"
#include "Vfx/Post/HIKARI_PostProfile.h"
#include "Scene/Components/HIKARI_ComponentLinkComponent.h"
#include "Scene/Components/HIKARI_VfxPlayerComponent.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_RenderSubmissionSystem.h"

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
        VFX::SetAssetRegistry(&assetRegistry_);
        ReloadSceneDocument();
        if (RebuildRuntimeWorld()) {
            systemScheduler_.Clear();
            RegisterDefaultSystems();
            systemScheduler_.AttachWorld(world_);
        }
    }

    void DocumentSceneBase::OnExit() {
        systemScheduler_.DetachWorld(world_);
        systemScheduler_.Clear();
        RuntimeSceneContext::SetCurrentWorld(nullptr);
    }

    void DocumentSceneBase::Update(float dt) {
        const FrameContext& frame = HIKARI::TIME::GetFrameContext();
        if (UseDebugCamera()) {
            debugCamera_.Update(dt, camera_);
        }
        systemScheduler_.PreUpdate(world_, frame);
        world_.Update(dt);
        systemScheduler_.Update(world_, frame);
        systemScheduler_.LateUpdate(world_, frame);
    }

    void DocumentSceneBase::Render() {
        RENDERER3D::Reset();
        MODELRENDERER::Reset();
        SKYRENDERER::Reset();

        if (environment_.post.enabled && !environment_.post.globalPostProfileId.empty()) {
            if (!environment_.post.valuesInitialized) {
                PostProfile profile{};
                if (PostProfile::LoadById(environment_.post.globalPostProfileId, profile)) {
                    profile.CopyValuesTo(environment_.post.paramValues);
                    environment_.post.valuesInitialized = true;
                }
            }
            POST::PostSystem::SetGlobalProfile(environment_.post.globalPostProfileId, environment_.post.paramValues);
        } else {
            POST::PostSystem::ClearGlobalProfile();
        }

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
        const FrameContext& frame = HIKARI::TIME::GetFrameContext();
        systemScheduler_.PreRender(world_, frame);
        systemScheduler_.Render(world_, frame);
        systemScheduler_.PostRender(world_, frame);

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

        componentGizmoRenderer_.SubmitWorldGizmos(world_, componentGizmoState_, selectedGizmoObjectId_);

        MODELRENDERER::RenderAll(camera_, activeEnvironment);
        VFX::Render(camera_);
        RENDERER3D::RenderAll(camera_, static_cast<float>(kScreenW), static_cast<float>(kScreenH));
    }

    void DocumentSceneBase::RenderImGui() {
        world_.RenderImGui();
        componentGizmoRenderer_.DrawScreenSpaceGizmos(world_, componentGizmoState_, selectedGizmoObjectId_);
    }

    const std::string& DocumentSceneBase::GetSceneId() const {
        return sceneId_;
    }

    const std::string& DocumentSceneBase::GetScenePath() const {
        return scenePath_;
    }

    SceneCatalog& DocumentSceneBase::GetSceneCatalog() {
        return sceneCatalog_;
    }

    const SceneCatalog& DocumentSceneBase::GetSceneCatalog() const {
        return sceneCatalog_;
    }

    void DocumentSceneBase::SetSceneId(std::string sceneId) {
        sceneId_ = std::move(sceneId);
    }

    void DocumentSceneBase::SetScenePath(std::string scenePath) {
        scenePath_ = std::move(scenePath);
    }

    World& DocumentSceneBase::GetWorld() {
        return world_;
    }

    const World& DocumentSceneBase::GetWorld() const {
        return world_;
    }

    SceneDocument& DocumentSceneBase::GetSceneDocument() {
        return sceneDocument_;
    }

    const SceneDocument& DocumentSceneBase::GetSceneDocument() const {
        return sceneDocument_;
    }

    SceneEnvironment& DocumentSceneBase::GetSceneEnvironment() {
        return environment_;
    }

    const SceneEnvironment& DocumentSceneBase::GetSceneEnvironment() const {
        return environment_;
    }

    AssetRegistry& DocumentSceneBase::GetAssetRegistry() {
        return assetRegistry_;
    }

    ModelManager& DocumentSceneBase::GetModelManager() {
        return modelManager_;
    }

    SkyManager& DocumentSceneBase::GetSkyManager() {
        return skyManager_;
    }

    ComponentRegistry& DocumentSceneBase::GetComponentRegistry() {
        return componentRegistry_;
    }

    Camera3D& DocumentSceneBase::GetCamera() {
        return camera_;
    }

    const Camera3D& DocumentSceneBase::GetCamera() const {
        return camera_;
    }

    DebugCameraController3D& DocumentSceneBase::GetDebugCamera() {
        return debugCamera_;
    }

    bool& DocumentSceneBase::GetEnvironmentLightingEnabled() {
        return environmentLightingEnabled_;
    }


    void DocumentSceneBase::SetComponentGizmoState(const ComponentGizmoState& state) {
        componentGizmoState_ = state;
    }

    void DocumentSceneBase::SetSelectedGizmoObjectId(SceneObjectId id) {
        selectedGizmoObjectId_ = id;
    }

    bool DocumentSceneBase::ReloadAssets() {
        assetRegistry_.Clear();
        const bool okModels = assetJsonLoader_.LoadModelDescriptors("Data/assets_models.json", assetRegistry_);
        const bool okSkies = assetJsonLoader_.LoadSkyDescriptors("Data/assets_skies.json", assetRegistry_);
        const bool okTextures = assetJsonLoader_.LoadTextureDescriptors("Data/assets_textures.json", assetRegistry_);
        const bool okVfx = assetJsonLoader_.LoadVfxDescriptors("Data/assets_vfx.json", assetRegistry_);
        return okModels && okSkies && okTextures && okVfx;
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

        RuntimeSceneContext::SetCurrentWorld(&world_);
        RuntimeSceneContext::ResolvePendingSceneEntry(world_, sceneId_);

        return built;
    }

    void DocumentSceneBase::RegisterDefaultSystems() {
        systemScheduler_.AddSystem(std::make_unique<RenderSubmissionSystem>());
    }

    void DocumentSceneBase::RegisterDefaultComponentTypes() {
        if (!componentRegistry_.Find("ModelComponent")) {
            componentRegistry_.Register(ComponentTypeInfo{
                "ModelComponent",
                []() -> std::unique_ptr<IComponent> { return std::make_unique<ModelComponent>(); },
                {},
                {},
                {},
                false
            });
        }

        if (!componentRegistry_.Find("UIButtonSceneTransitionComponent")) {
            componentRegistry_.Register(ComponentTypeInfo{
                "UIButtonSceneTransitionComponent",
                []() -> std::unique_ptr<IComponent> { return std::make_unique<UIButtonSceneTransitionComponent>(); },
                {},
                {},
                {},
                false,
                [](const SceneObjectData&, nlohmann::json& properties) {
                    properties["targetSceneId"] = "Title";
                    properties["transitionProfileId"] = "noise_wipe";
                    properties["screenRect"] = {
                        { "x", 100.0f },
                        { "y", 100.0f },
                        { "w", 200.0f },
                        { "h", 80.0f }
                    };
                }
            });
        }

        if (!componentRegistry_.Find("SpawnPointComponent")) {
            componentRegistry_.Register(ComponentTypeInfo{
                "SpawnPointComponent",
                []() -> std::unique_ptr<IComponent> { return std::make_unique<SpawnPointComponent>(); },
                {},
                {},
                {},
                false,
                [](const SceneObjectData& object, nlohmann::json& properties) {
                    properties["spawnPointId"] = object.name.empty() ? "DefaultSpawn" : object.name;
                    properties["enabled"] = true;
                }
            });
        }

        if (!componentRegistry_.Find("TriggerVolumeComponent")) {
            componentRegistry_.Register(ComponentTypeInfo{
                "TriggerVolumeComponent",
                []() -> std::unique_ptr<IComponent> { return std::make_unique<TriggerVolumeComponent>(); },
                {},
                {},
                {},
                false,
                [](const SceneObjectData&, nlohmann::json& properties) {
                    properties["enabled"] = true;
                    properties["boxSize"] = {
                        { "x", 1.0f },
                        { "y", 2.0f },
                        { "z", 1.0f }
                    };
                }
            });
        }


        if (!componentRegistry_.Find("VfxPlayerComponent")) {
            componentRegistry_.Register(ComponentTypeInfo{
                "VfxPlayerComponent",
                []() -> std::unique_ptr<IComponent> { return std::make_unique<VfxPlayerComponent>(); },
                {},
                {},
                {},
                false,
                [](const SceneObjectData&, nlohmann::json& properties) {
                    properties["enabled"] = true;
                    properties["visible"] = true;
                    properties["slots"] = nlohmann::json::array({
                        {
                            { "slotName", "Default" },
                            { "effectAssetId", "Laser01" },
                            { "loop", false },
                            { "autoPlay", false },
                            { "restartIfAlreadyPlaying", true }
                        }
                    });
                }
            });
        }

        if (!componentRegistry_.Find("ComponentLinkComponent")) {
            componentRegistry_.Register(ComponentTypeInfo{
                "ComponentLinkComponent",
                []() -> std::unique_ptr<IComponent> { return std::make_unique<ComponentLinkComponent>(); },
                {},
                {},
                {},
                false
            });
        }
        if (!componentRegistry_.Find("DoorTransitionComponent")) {
            componentRegistry_.Register(ComponentTypeInfo{
                "DoorTransitionComponent",
                []() -> std::unique_ptr<IComponent> { return std::make_unique<DoorTransitionComponent>(); },
                { "TriggerVolumeComponent" },
                {},
                {},
                false,
                [](const SceneObjectData&, nlohmann::json& properties) {
                    properties["targetSceneId"] = "";
                    properties["requireInteractKey"] = true;
                    properties["enabled"] = true;
                }
            });
        }
    }

    void DocumentSceneBase::RegisterDefaultSceneCatalogEntries() {
        sceneCatalog_.Register(SceneCatalogEntry{ "Sandbox", "SandboxScene", "Data/scenes/scene_sandbox.json", true, "Sandbox", SceneLifetimePolicy::ReloadOnEnter });
        sceneCatalog_.Register(SceneCatalogEntry{ "Empty", "GameDocumentScene", "Data/scenes/scene_empty.json", true, "Empty", SceneLifetimePolicy::ReloadOnEnter });
        sceneCatalog_.Register(SceneCatalogEntry{ "Title", "TitleScene", "Data/scenes/scene_title.json", true, "Title", SceneLifetimePolicy::ReloadOnEnter });

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

            sceneCatalog_.Register(SceneCatalogEntry{ stem, "GameDocumentScene", path.generic_string(), true, stem, SceneLifetimePolicy::ReloadOnEnter });
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
