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
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"

namespace HIKARI {

    class SandboxScene final : public DocumentSceneBase {
    public:
        SandboxScene(SceneCatalog& sceneCatalog, std::string sceneId = "Sandbox");

        void Update(float dt) override;
        void RenderImGui() override;
        const char* GetSceneName() const override { return "SandboxScene"; }

    private:
        DebugWindowState debugWindowState_{};
        DebugMenuBar debugMenuBar_{};
        HierarchyPanel hierarchyPanel_{};
        InspectorPanel inspectorPanel_{};
        AssetBrowserPanel assetBrowserPanel_{};
        StatsPanel statsPanel_{};
        EnvironmentPanel environmentPanel_{};
        DebugCameraPanel debugCameraPanel_{};

        EditorSelection selection_{};
        std::string sceneNameEditBuffer_{ "Untitled" };
        std::string saveAsNameBuffer_{ "Untitled" };
        uint64_t nextSceneObjectId_ = 1;
        bool sceneDirty_ = false;

        void MarkSceneDirty();
        bool IsSceneDirty() const;
        void DrawDocumentToolbar();
        SceneObjectData* FindDocumentObjectById(SceneObjectId id);
        GameObject* FindRuntimeObjectByDocumentId(SceneObjectId id);
        void SyncSelectedObjectBackToDocument();
        SceneObjectData* FindDocumentObjectByRuntime(GameObject* runtimeObject);

        bool DrawDebugHelpers() const override { return true; }
        bool UseEnvironmentLighting() const override { return environmentLightingEnabled_; }
    };

} // namespace HIKARI
