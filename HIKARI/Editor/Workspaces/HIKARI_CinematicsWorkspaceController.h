#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "Editor/Views/HIKARI_DirectorViewPanel.h"
#include "Editor/Workspaces/HIKARI_CameraOverviewPanel.h"
#include "Editor/Workspaces/HIKARI_CameraTimelinePanel.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Scene/HIKARI_CameraDirector.h"
#include "Scene/HIKARI_SceneObjectId.h"

namespace HIKARI {

    class DocumentSceneBase;
    struct EditorContext;
    class SelectionSyncService;

    namespace EDITOR {
        class EditorPlaySession;

        struct CinematicsWorkspaceResult {
            bool toggleGamePreviewRequested = false;
            bool cinematicsChanged = false;
            uint64_t timelineEditMergeId = 0;
            std::string statusMessage{};
        };

        class CinematicsWorkspaceController {
        public:
            void DrawDockSpace(bool resetDefaultDockLayout) const;
            void SyncSceneIdentity(
                DocumentSceneBase& scene,
                EditorWorkspaceHost& workspaceHost);
            void PrepareForRuntimePlay();
            void OnCinematicsDocumentRestored(
                DocumentSceneBase& scene,
                EditorWorkspaceHost& workspaceHost);
            void ApplyWorkspaceActivation(
                DocumentSceneBase& scene,
                const EditorWorkspaceActivation& activation,
                EditorContext& context,
                EditorWorkspaceHost& workspaceHost);
            CinematicsWorkspaceResult Draw(
                DocumentSceneBase& scene,
                EditorPlaySession& playSession,
                EditorContext& context,
                SelectionSyncService& selectionSync,
                EditorWorkspaceHost& workspaceHost);

        private:
            void DrawDirectorViewWindow(
                DocumentSceneBase& scene,
                EditorContext& context,
                SelectionSyncService& selectionSync,
                EditorWorkspaceHost& workspaceHost);
            bool DrawGameViewWindow(
                DocumentSceneBase& scene,
                EditorPlaySession& playSession,
                EditorWorkspaceHost& workspaceHost);
            void DrawMapOverviewWindow(
                DocumentSceneBase& scene,
                EditorContext& context,
                EditorWorkspaceHost& workspaceHost,
                CinematicsWorkspaceResult& result);
            void DrawCameraListWindow(
                DocumentSceneBase& scene,
                EditorContext& context,
                EditorWorkspaceHost& workspaceHost,
                CinematicsWorkspaceResult& result);
            void DrawTimelineWindow(
                DocumentSceneBase& scene,
                EditorContext& context,
                EditorWorkspaceHost& workspaceHost,
                CinematicsWorkspaceResult& result);

            void ApplyCameraOverviewAction(
                DocumentSceneBase& scene,
                const CameraOverviewAction& action,
                EditorContext& context,
                EditorWorkspaceHost& workspaceHost,
                CinematicsWorkspaceResult& result);
            void ApplyDirectorViewResult(
                DocumentSceneBase& scene,
                const DirectorViewPanelResult& result,
                EditorContext& context,
                SelectionSyncService& selectionSync);
            void ApplyCameraTimelineResult(
                DocumentSceneBase& scene,
                const CameraTimelinePanelResult& result,
                EditorContext& context,
                EditorWorkspaceHost& workspaceHost);
            bool BindCameraPreview(
                DocumentSceneBase& scene,
                SceneObjectId cameraObjectId,
                EditorWorkspaceHost& workspaceHost,
                const CameraBlendDesc& blend = {},
                bool forceRebind = false);
            void RestoreTimelinePreview(
                DocumentSceneBase& scene,
                EditorWorkspaceHost& workspaceHost);
            void ClearCameraBinding(EditorWorkspaceHost& workspaceHost);

            CameraOverviewPanel cameraOverviewPanel_{};
            CameraTimelinePanel cameraTimelinePanel_{};
            DirectorViewPanel directorViewPanel_{};
            bool cameraPreviewOwned_ = false;
            bool timelinePreviewOwned_ = false;
            uint64_t timelinePreviewShotId_ = 0;
            std::optional<SceneObjectId> preWorkspacePreviewObjectId_{};
            std::optional<SceneObjectId> boundCameraObjectId_{};
            std::optional<SceneObjectId> timelineRestoreCameraObjectId_{};
            std::string sceneIdentity_{};
            uint64_t sceneDocumentRevision_ = 0;
        };

    } // namespace EDITOR
} // namespace HIKARI
