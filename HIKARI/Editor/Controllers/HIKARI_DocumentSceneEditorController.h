#pragma once

#include <optional>

#include "Editor/HIKARI_DebugCameraPanel.h"
#include "Editor/HIKARI_DebugMenuBar.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/History/HIKARI_EditorDocumentHistory.h"
#include "Editor/History/HIKARI_SceneObjectTransformHistorySession.h"
#include "Editor/SystemAuthoring/HIKARI_SystemAuthoringRegistry.h"
#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"
#include "Editor/Panels/HIKARI_EnvironmentPanel.h"
#include "Editor/HIKARI_HierarchyPanel.h"
#include "Editor/Commands/HIKARI_SceneObjectCommandService.h"
#include "Editor/Panels/HIKARI_SceneCreationPanel.h"
#include "Editor/Panels/HIKARI_SceneInspectorPanel.h"
#include "Editor/Panels/HIKARI_PerformanceAuditPanel.h"
#include "Editor/Panels/HIKARI_QualityPanel.h"
#include "Editor/Panels/HIKARI_ResourceWorkspacePanel.h"
#include "Editor/Panels/HIKARI_ValidationLabPanel.h"
#include "Editor/Tools/HIKARI_EditorToolHost.h"
#include "Editor/Viewport/HIKARI_SceneViewportSelectionService.h"
#include "Editor/Windows/HIKARI_SceneAuthoringUtilityWindows.h"
#include "Editor/Workspaces/HIKARI_CinematicsWorkspaceController.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Editor/Workspaces/HIKARI_ModelCollisionWorkspaceController.h"
#include "Editor/Workspaces/HIKARI_AnimationStateMachineWorkspaceController.h"
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
        EDITOR::SceneObjectCommandService sceneObjectCommands_{};
        EDITOR::SceneCreationPanel sceneCreationPanel_{};
        EDITOR::SceneInspectorPanel sceneInspectorPanel_{};
        EDITOR::SceneViewportSelectionService viewportSelectionService_{};

        HierarchyPanel hierarchyPanel_{};
        TimePanel timePanel_{};
        ResourceWorkspacePanel resourceWorkspacePanel_{};
        StatsPanel statsPanel_{};
        PerformanceAuditPanel performanceAuditPanel_{};
        EDITOR::SceneAuthoringUtilityWindows sceneAuthoringUtilityWindows_{};
        ValidationLabPanel validationLabPanel_{};
        EnvironmentPanel environmentPanel_{};
        QualityPanel qualityPanel_{};
        EDITOR::EditorToolHost toolHost_{};
        EDITOR::SystemAuthoringRegistry systemAuthoringRegistry_{};
        EDITOR::EditorWorkspaceHost workspaceHost_{};
        EDITOR::CinematicsWorkspaceController cinematicsWorkspaceController_{};
        EDITOR::ModelCollisionWorkspaceController modelCollisionWorkspaceController_{};
        EDITOR::AnimationStateMachineWorkspaceController
            animationStateMachineWorkspaceController_{};
        DebugCameraPanel debugCameraPanel_{};
        EDITOR::EditorTransformGizmo transformGizmo_{};
        void DrawGameViewportWindow(
            DocumentSceneBase& scene,
            EDITOR::EditorPlaySession& playSession);
        void ToggleGamePreview(
            DocumentSceneBase& scene,
            EDITOR::EditorPlaySession& playSession);
        void LaunchWindowedGamePreview(
            DocumentSceneBase& scene,
            EDITOR::EditorPlaySession& playSession);
        bool PrepareGamePreview(DocumentSceneBase& scene);
        bool SaveRenderQualityProfile(DocumentSceneBase& scene);
        void DrawSceneWorkspaceWindow(DocumentSceneBase& scene);
        void DrawInspectorWindow(DocumentSceneBase& scene);
        void SelectViewportObject(
            DocumentSceneBase& scene,
            SceneObjectId objectId);
        void DrawViewportContextMenu(DocumentSceneBase& scene);
        void DrawDebugWorkspaceWindow(DocumentSceneBase& scene);
        void DrawDebugViewWindow(DocumentSceneBase& scene, bool& open);
        void HandleGameViewportAssetDrop(DocumentSceneBase& scene);
        void DrawPendingSceneOpenModal(DocumentSceneBase& scene);
        bool OpenSceneAssetFromEditor(DocumentSceneBase& scene, const AssetGuid& sceneGuid);
        void SyncDocumentHistory(DocumentSceneBase& scene);
        void HandleGlobalDocumentShortcuts(DocumentSceneBase& scene);
        void SaveCurrentDocument(DocumentSceneBase& scene);
        void ExecuteDocumentHistory(
            DocumentSceneBase& scene,
            bool redo);
        void ApplyHistoryResult(
            DocumentSceneBase& scene,
            const EDITOR::EditorHistoryResult& result,
            bool redo);
        void RecordCinematicsHistory(
            DocumentSceneBase& scene,
            SceneCinematicsSettings before,
            uint64_t mergeGroup,
            bool externalDirtyBefore);

        AssetGuid pendingSceneOpenGuid_{};
        std::string viewportDropMessage_{};
        bool renderQualitySavePending_ = false;
        EDITOR::EditorDocumentHistory documentHistory_{};
        EDITOR::SceneObjectTransformHistorySession
            viewportTransformHistory_{};
        bool historyExternalDirty_ = false;
    };

} // namespace HIKARI
