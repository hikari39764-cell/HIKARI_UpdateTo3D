#pragma once

#include "Editor/HIKARI_DebugCameraPanel.h"
#include "Editor/HIKARI_DebugMenuBar.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"
#include "Editor/HIKARI_EnvironmentPanel.h"
#include "Editor/HIKARI_HierarchyPanel.h"
#include "Editor/HIKARI_InspectorPanel.h"
#include "Editor/Panels/HIKARI_ResourceWorkspacePanel.h"
#include "Editor/HIKARI_SceneObjectAuthoringPanel.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Editor/HIKARI_StatsPanel.h"
#include "Editor/HIKARI_DocumentToolbarController.h"
#include "Editor/HIKARI_TimePanel.h"
#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI {

    class DocumentSceneBase;

    class DocumentSceneEditorController {
    public:
        void Draw(DocumentSceneBase& scene);

    private:
        EditorContext context_{};
        SelectionSyncService selectionSync_{};

        DebugMenuBar debugMenuBar_{};
        DocumentToolbarController documentToolbarController_{};
        SceneObjectAuthoringPanel sceneObjectAuthoringPanel_{};

        HierarchyPanel hierarchyPanel_{};
        TimePanel timePanel_{};
        InspectorPanel inspectorPanel_{};
        ResourceWorkspacePanel resourceWorkspacePanel_{};
        StatsPanel statsPanel_{};
        EnvironmentPanel environmentPanel_{};
        DebugCameraPanel debugCameraPanel_{};
        EDITOR::EditorTransformGizmo transformGizmo_{};

        void DrawGameViewportWindow(DocumentSceneBase& scene, bool gameOnly);
        void DrawSceneWorkspaceWindow(DocumentSceneBase& scene);
        void DrawDebugWorkspaceWindow(DocumentSceneBase& scene);
        void HandleGameViewportAssetDrop(DocumentSceneBase& scene);
        void DrawPendingSceneOpenModal(DocumentSceneBase& scene);
        bool OpenSceneAssetFromEditor(DocumentSceneBase& scene, const AssetGuid& sceneGuid);

        AssetGuid pendingSceneOpenGuid_{};
        std::string viewportDropMessage_{};
    };

} // namespace HIKARI
