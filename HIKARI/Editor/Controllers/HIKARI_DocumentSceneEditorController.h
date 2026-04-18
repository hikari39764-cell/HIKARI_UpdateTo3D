#pragma once

#include "Editor/HIKARI_AssetBrowserPanel.h"
#include "Editor/HIKARI_DebugCameraPanel.h"
#include "Editor/HIKARI_DebugMenuBar.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_EnvironmentPanel.h"
#include "Editor/HIKARI_HierarchyPanel.h"
#include "Editor/HIKARI_InspectorPanel.h"
#include "Editor/HIKARI_SceneObjectAuthoringPanel.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Editor/HIKARI_StatsPanel.h"
#include "Editor/HIKARI_DocumentToolbarController.h"

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
        InspectorPanel inspectorPanel_{};
        AssetBrowserPanel assetBrowserPanel_{};
        StatsPanel statsPanel_{};
        EnvironmentPanel environmentPanel_{};
        DebugCameraPanel debugCameraPanel_{};

        void DrawSceneWorkspaceWindow(DocumentSceneBase& scene);
        void DrawDebugWorkspaceWindow(DocumentSceneBase& scene);
    };

} // namespace HIKARI
