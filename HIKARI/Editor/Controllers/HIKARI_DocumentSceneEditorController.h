#pragma once

#include <optional>

#include "Editor/HIKARI_DebugCameraPanel.h"
#include "Editor/HIKARI_DebugMenuBar.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"
#include "Editor/Panels/HIKARI_EnvironmentPanel.h"
#include "Editor/HIKARI_HierarchyPanel.h"
#include "Editor/HIKARI_InspectorPanel.h"
#include "Editor/Panels/HIKARI_PerformanceAuditPanel.h"
#include "Editor/Panels/HIKARI_QualityPanel.h"
#include "Editor/Panels/HIKARI_ResourceWorkspacePanel.h"
#include "Editor/Panels/HIKARI_ValidationLabPanel.h"
#include "Editor/Tools/HIKARI_EditorToolHost.h"
#include "Editor/Workspaces/HIKARI_CameraOverviewPanel.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Editor/HIKARI_SceneObjectAuthoringPanel.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Editor/HIKARI_StatsPanel.h"
#include "Editor/Controllers/HIKARI_DocumentToolbarController.h"
#include "Editor/HIKARI_TimePanel.h"
#include "Assets/HIKARI_AssetGuid.h"

namespace HIKARI {

    class DocumentSceneBase;
    namespace EDITOR {
        class EditorPlaySession;
    }

    class DocumentSceneEditorController {
    public:
        DocumentSceneEditorController();

        void Draw(
            DocumentSceneBase& scene,
            EDITOR::EditorPlaySession& playSession);

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
        PerformanceAuditPanel performanceAuditPanel_{};
        ValidationLabPanel validationLabPanel_{};
        EnvironmentPanel environmentPanel_{};
        QualityPanel qualityPanel_{};
        EDITOR::EditorToolHost toolHost_{};
        EDITOR::EditorWorkspaceHost workspaceHost_{};
        EDITOR::CameraOverviewPanel cameraOverviewPanel_{};
        DebugCameraPanel debugCameraPanel_{};
        EDITOR::EditorTransformGizmo transformGizmo_{};
        void DrawGameViewportWindow(
            DocumentSceneBase& scene,
            bool gameOnly,
            EDITOR::EditorPlaySession& playSession);
        void DrawCinematicsWorkspace(
            DocumentSceneBase& scene,
            EDITOR::EditorPlaySession& playSession);
        void DrawCinematicsGameViewWindow(
            DocumentSceneBase& scene,
            EDITOR::EditorPlaySession& playSession);
        void DrawCinematicsOverviewWindow(DocumentSceneBase& scene);
        void DrawCinematicsCameraListWindow(DocumentSceneBase& scene);
        void ApplyWorkspaceActivation(
            DocumentSceneBase& scene,
            const EDITOR::EditorWorkspaceActivation& activation);
        void ApplyCameraOverviewAction(
            DocumentSceneBase& scene,
            const EDITOR::CameraOverviewAction& action);
        void ClearCinematicsCameraBinding();
        void SyncCinematicsSceneIdentity(DocumentSceneBase& scene);
        void ToggleGamePreview(
            DocumentSceneBase& scene,
            EDITOR::EditorPlaySession& playSession);
        void LaunchStandaloneGamePreview(
            DocumentSceneBase& scene,
            EDITOR::EditorPlaySession& playSession);
        bool PrepareGamePreview(DocumentSceneBase& scene);
        bool SaveRenderQualityProfile(DocumentSceneBase& scene);
        void DrawSceneWorkspaceWindow(DocumentSceneBase& scene);
        void DrawDebugWorkspaceWindow(DocumentSceneBase& scene);
        void DrawDebugViewWindow(DocumentSceneBase& scene, bool& open);
        void HandleGameViewportAssetDrop(DocumentSceneBase& scene);
        void DrawPendingSceneOpenModal(DocumentSceneBase& scene);
        bool OpenSceneAssetFromEditor(DocumentSceneBase& scene, const AssetGuid& sceneGuid);

        AssetGuid pendingSceneOpenGuid_{};
        std::string viewportDropMessage_{};
        bool renderQualitySavePending_ = false;
        bool cinematicsCameraPreviewOwned_ = false;
        std::optional<SceneObjectId> preCinematicsPreviewObjectId_{};
        std::optional<SceneObjectId> cinematicsBoundCameraObjectId_{};
        std::string cinematicsSceneIdentity_{};
        uint64_t cinematicsSceneDocumentRevision_ = 0;
    };

} // namespace HIKARI
