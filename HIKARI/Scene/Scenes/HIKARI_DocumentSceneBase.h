#pragma once

#include <string>

#include "Assets/HIKARI_AssetJsonLoader.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Render3D/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_DebugCameraController3D.h"
#include "Render3D/HIKARI_ModelManager.h"
#include "Render3D/HIKARI_SceneEnvironment.h"
#include "Render3D/HIKARI_SkyManager.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_IScene.h"
#include "Scene/HIKARI_SceneCatalog.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"

namespace HIKARI {

    class DocumentSceneBase : public IScene {
    public:
        DocumentSceneBase(SceneCatalog& sceneCatalog, std::string sceneId);

        void OnEnter() override;
        void OnExit() override;
        void Update(float dt) override;
        void Render() override;
        void RenderImGui() override;

        const std::string& GetSceneId() const;

    protected:
        bool ReloadAssets();
        bool ReloadSceneDocument();
        bool RebuildRuntimeWorld();
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
    };

} // namespace HIKARI
