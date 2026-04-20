#pragma once

#include <string>

#include "Assets/HIKARI_AssetJsonLoader.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Render3D/Core/HIKARI_Camera3D.h"
#include "Render3D/Debug/HIKARI_DebugCameraController3D.h"
#include "Render3D/Core/HIKARI_ModelManager.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyManager.h"
#include "Runtime/Scene/HIKARI_ComponentRegistry.h"
#include "Runtime/Scene/HIKARI_IScene.h"
#include "Runtime/Scene/HIKARI_SceneCatalog.h"
#include "Authoring/Scene/HIKARI_SceneDocument.h"
#include "Authoring/Scene/HIKARI_SceneRuntimeBuilder.h"
#include "Runtime/Core/HIKARI_World.h"
#include "Authoring/Serialization/HIKARI_SceneSerializer.h"
#include "Runtime/Scene/Debug/HIKARI_ComponentGizmoRenderer.h"
#include "Runtime/Systems/HIKARI_SystemScheduler.h"
#include "Services/Frame/HIKARI_FrameContext.h"

namespace HIKARI {

    class DocumentSceneBase : public IScene {
    public:
        DocumentSceneBase(SceneCatalog& sceneCatalog, std::string sceneId);

        void OnEnter() override;
        void OnExit() override;
        void Update(float dt) override;
        void Render() override;
        void RenderImGui() override;

        const std::string& GetSceneId() const override;
        const std::string& GetScenePath() const;
        SceneCatalog& GetSceneCatalog();
        const SceneCatalog& GetSceneCatalog() const;
        void SetSceneId(std::string sceneId);
        void SetScenePath(std::string scenePath);

        World& GetWorld();
        const World& GetWorld() const;

        SceneDocument& GetSceneDocument();
        const SceneDocument& GetSceneDocument() const;

        SceneEnvironment& GetSceneEnvironment();
        const SceneEnvironment& GetSceneEnvironment() const;
        AssetRegistry& GetAssetRegistry();
        ModelManager& GetModelManager();
        SkyManager& GetSkyManager();
        ComponentRegistry& GetComponentRegistry();

        Camera3D& GetCamera();
        const Camera3D& GetCamera() const;
        DebugCameraController3D& GetDebugCamera();

        bool& GetEnvironmentLightingEnabled();
        bool ReloadAssets();
        bool ReloadSceneDocument();
        bool RebuildRuntimeWorld();

        void SetComponentGizmoState(const ComponentGizmoState& state);
        void SetSelectedGizmoObjectId(SceneObjectId id);

    protected:
        void RegisterDefaultComponentTypes();
        void RegisterDefaultSceneCatalogEntries();

        virtual bool UseDebugCamera() const;
        virtual bool DrawDebugHelpers() const;
        virtual bool UseEnvironmentLighting() const;

    protected:
        SceneCatalog& sceneCatalog_;
        std::string sceneId_;
        std::string scenePath_{};

        Camera3D camera_{};
        DebugCameraController3D debugCamera_{};
        World world_{};
        ModelManager modelManager_{};
        SkyManager skyManager_{};
        SceneEnvironment environment_{};
        bool environmentLightingEnabled_ = true;

        AssetRegistry assetRegistry_{};
        AssetJsonLoader assetJsonLoader_{};
        ComponentRegistry componentRegistry_{};
        SceneSerializer sceneSerializer_{};
        SceneRuntimeBuilder runtimeBuilder_{};
        SceneDocument sceneDocument_{};
        SystemScheduler systemScheduler_{};
        FrameContext frameContext_{};
        ComponentGizmoRenderer componentGizmoRenderer_{};
        ComponentGizmoState componentGizmoState_{};
        SceneObjectId selectedGizmoObjectId_{};
    };

} // namespace HIKARI
