#pragma once

#include "HIKARI_AssetBrowserPanel.h"
#include "HIKARI_DebugCameraPanel.h"
#include "HIKARI_DebugMenuBar.h"
#include "HIKARI_EditorContext.h"
#include "HIKARI_EnvironmentPanel.h"
#include "HIKARI_HierarchyPanel.h"
#include "HIKARI_InspectorPanel.h"
#include "HIKARI_SceneObjectAuthoringPanel.h"
#include "HIKARI_SelectionSyncService.h"
#include "HIKARI_StatsPanel.h"
#include "HIKARI_DocumentToolbarController.h"

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

        void DrawGizmoSettingsWindow();
    };

} // namespace HIKARI
