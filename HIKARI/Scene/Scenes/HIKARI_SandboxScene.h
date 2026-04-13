#pragma once
#include "Editor/HIKARI_AssetBrowserPanel.h"
#include "Editor/HIKARI_DebugCameraPanel.h"
#include "Editor/HIKARI_DebugMenuBar.h"
#include "Editor/HIKARI_DebugWindowState.h"
#include "Editor/HIKARI_EditorSelection.h"
#include "Editor/HIKARI_EnvironmentPanel.h"
#include "Editor/HIKARI_HierarchyPanel.h"
#include "Editor/HIKARI_InspectorPanel.h"
#include "Editor/HIKARI_StatsPanel.h"
#include "Render3D/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_DebugCameraController3D.h"
#include "Render3D/HIKARI_ModelManager.h"
#include "Render3D/HIKARI_SceneEnvironment.h"
#include "Render3D/HIKARI_SkyManager.h"
#include "Assets/HIKARI_AssetJsonLoader.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Scene/HIKARI_ComponentRegistry.h"
#include "Scene/HIKARI_SceneDocument.h"
#include "Scene/HIKARI_SceneRegistry.h"
#include "Scene/HIKARI_SceneRuntimeBuilder.h"
#include "Scene/Serialization/HIKARI_SceneSerializer.h"
#include "Scene/HIKARI_IScene.h"
#include "Scene/HIKARI_World.h"

namespace HIKARI {

    class SandboxScene final : public IScene {
    public:
        void OnEnter() override;
        void OnExit() override;
        void Update(float dt) override;
        void Render() override;
        void RenderImGui() override;
        const char* GetSceneName() const override { return "SandboxScene"; }

    private:
        Camera3D camera_{};
        World world_{};
        ModelManager modelManager_{};
        SkyManager skyManager_{};
        EditorSelection selection_{};
        DebugCameraController3D debugCamera_{};
        SceneEnvironment environment_{};

        DebugWindowState debugWindowState_{};
        DebugMenuBar debugMenuBar_{};
        HierarchyPanel hierarchyPanel_{};
        InspectorPanel inspectorPanel_{};
        AssetBrowserPanel assetBrowserPanel_{};
        StatsPanel statsPanel_{};
        EnvironmentPanel environmentPanel_{};
        DebugCameraPanel debugCameraPanel_{};

        bool environmentLightingEnabled_ = true;

        AssetRegistry assetRegistry_{};
        AssetJsonLoader assetJsonLoader_{};
        ComponentRegistry componentRegistry_{};
        SceneSerializer sceneSerializer_{};
        SceneRuntimeBuilder runtimeBuilder_{};
        SceneRegistry sceneRegistry_{};
        SceneDocument sceneDocument_{};
        std::string currentSceneId_{ "Sandbox" };
        std::string currentScenePath_{};
        uint64_t nextSceneObjectId_ = 1;
        bool sceneDirty_ = false;

        bool ReloadAssets();
        bool ReloadSceneDocument();
        bool RebuildRuntimeWorld();
        void MarkSceneDirty();
        bool IsSceneDirty() const;
        void EnsureComponentRegistry();
        void EnsureSceneRegistry();
        void DrawDocumentToolbar();
        SceneObjectData* FindDocumentObjectById(SceneObjectId id);
        SceneObjectData* FindDocumentObjectByRuntime(GameObject* runtimeObject);
    };

} // namespace HIKARI
