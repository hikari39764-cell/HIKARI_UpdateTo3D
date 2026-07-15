#include "Editor/Workspaces/HIKARI_CinematicsWorkspaceController.h"

#include <algorithm>
#include <cmath>

#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

    namespace {
        bool IsEnabledCameraObject(
            const DocumentSceneBase& scene,
            SceneObjectId cameraObjectId) {

            if (cameraObjectId.value == 0) {
                return false;
            }
            for (const auto& object : scene.GetWorld().GetObjects()) {
                if (!object || !(object->GetDocumentId() == cameraObjectId)) {
                    continue;
                }
                const CameraComponent* camera =
                    object->GetComponent<CameraComponent>();
                return camera != nullptr && camera->IsEnabled();
            }
            return false;
        }

        GameObject* FindRuntimeObject(
            DocumentSceneBase& scene,
            SceneObjectId objectId) {

            if (objectId.value == 0) {
                return nullptr;
            }
            for (const auto& object : scene.GetWorld().GetObjects()) {
                if (object && object->GetDocumentId() == objectId) {
                    return object.get();
                }
            }
            return nullptr;
        }

        DirectorCameraPose CameraPoseFromView(const Camera3D& camera) {
            MATH::Vec3 forward = camera.GetTarget() - camera.GetPosition();
            if (MATH::Length(forward) <= 1.0e-5f) {
                forward = { 0.0f, 0.0f, 1.0f };
            } else {
                forward = MATH::Normalize(forward);
            }
            const float yaw = std::atan2(forward.x, forward.z);
            const float pitch = std::asin(
                std::clamp(forward.y, -1.0f, 1.0f));

            DirectorCameraPose pose{};
            pose.position = camera.GetPosition();
            pose.rotation = MATH::Quat::FromEulerXYZ(-pitch, yaw, 0.0f);
            return pose;
        }
    }

    void CinematicsWorkspaceController::PrepareForRuntimePlay() {
        directorViewPanel_.ExitPilot();
    }

    void CinematicsWorkspaceController::ApplyWorkspaceActivation(
        DocumentSceneBase& scene,
        const EditorWorkspaceActivation& activation,
        EditorContext& context,
        EditorWorkspaceHost& workspaceHost) {

        if (activation.previous == EditorWorkspaceId::Cinematics &&
            activation.current != EditorWorkspaceId::Cinematics) {
            cameraTimelinePanel_.PausePlayback();
            timelinePreviewOwned_ = false;
            timelineRestoreCameraObjectId_.reset();
            if (cameraPreviewOwned_) {
                scene.EndEditorCameraPreview();
                if (preWorkspacePreviewObjectId_ &&
                    !scene.IsRuntimePlayActive()) {
                    (void)scene.BeginEditorCameraPreview(
                        *preWorkspacePreviewObjectId_);
                }
            }
            cameraPreviewOwned_ = false;
            preWorkspacePreviewObjectId_.reset();
            ClearCameraBinding(workspaceHost);
            directorViewPanel_.LeaveWorkspace();
            return;
        }

        if (activation.current != EditorWorkspaceId::Cinematics) {
            return;
        }
        if (activation.previous == EditorWorkspaceId::Cinematics &&
            !activation.targetCameraObjectId) {
            return;
        }

        context.windows.viewport.gameOnlyMode = false;
        if (activation.previous != EditorWorkspaceId::Cinematics) {
            cameraPreviewOwned_ = false;
            preWorkspacePreviewObjectId_.reset();
            if (scene.IsEditorCameraPreviewActive()) {
                preWorkspacePreviewObjectId_ =
                    scene.GetEditorCameraPreviewObjectId();
            }
        }

        std::optional<SceneObjectId> targetCamera =
            activation.targetCameraObjectId;
        if (!targetCamera &&
            activation.previous != EditorWorkspaceId::Cinematics) {
            targetCamera = preWorkspacePreviewObjectId_
                ? preWorkspacePreviewObjectId_
                : scene.GetSceneDocument().camera.defaultCameraObjectId;
        }
        if (!targetCamera ||
            !IsEnabledCameraObject(scene, *targetCamera)) {
            std::optional<SceneObjectId> activePreviewCamera;
            if (scene.IsEditorCameraPreviewActive()) {
                activePreviewCamera =
                    scene.GetEditorCameraPreviewObjectId();
            }
            if (activePreviewCamera &&
                IsEnabledCameraObject(scene, *activePreviewCamera)) {
                targetCamera = activePreviewCamera;
            } else {
                if (cameraPreviewOwned_ && !scene.IsRuntimePlayActive()) {
                    scene.EndEditorCameraPreview();
                }
                cameraPreviewOwned_ = false;
                ClearCameraBinding(workspaceHost);
                return;
            }
        }

        boundCameraObjectId_ = *targetCamera;
        directorViewPanel_.SetTargetCamera(*targetCamera);
        EditorViewInstance& gameView =
            workspaceHost.GetCinematicsGameView();
        gameView.cameraBinding.kind =
            EditorViewCameraSourceKind::SceneCameraObject;
        gameView.cameraBinding.sceneObjectId = *targetCamera;

        if (!scene.IsRuntimePlayActive() &&
            (!scene.IsEditorCameraPreviewActive() ||
                !(scene.GetEditorCameraPreviewObjectId() == *targetCamera))) {
            cameraPreviewOwned_ =
                scene.BeginEditorCameraPreview(*targetCamera);
            if (!cameraPreviewOwned_) {
                ClearCameraBinding(workspaceHost);
            }
        }
    }

    void CinematicsWorkspaceController::SyncSceneIdentity(
        DocumentSceneBase& scene,
        EditorWorkspaceHost& workspaceHost) {

        const uint64_t currentRevision = scene.GetSceneDocumentRevision();
        const AssetGuid& sceneGuid = scene.GetCurrentSceneAssetGuid();
        const std::string currentIdentity = sceneGuid.IsValid()
            ? "Asset:" + sceneGuid.value
            : "Transient:" + scene.GetSceneId() + ":" +
                std::to_string(currentRevision);
        if (sceneIdentity_.empty()) {
            sceneIdentity_ = currentIdentity;
            sceneDocumentRevision_ = currentRevision;
            return;
        }

        const bool sceneChanged = sceneIdentity_ != currentIdentity;
        const bool documentReloaded =
            sceneDocumentRevision_ != currentRevision;
        if (!sceneChanged && !documentReloaded) {
            return;
        }

        directorViewPanel_.ResetForScene();
        cameraTimelinePanel_.ResetForScene();
        cameraPreviewOwned_ = false;
        timelinePreviewOwned_ = false;
        preWorkspacePreviewObjectId_.reset();
        timelineRestoreCameraObjectId_.reset();
        if (sceneChanged ||
            (boundCameraObjectId_ &&
                !IsEnabledCameraObject(scene, *boundCameraObjectId_))) {
            ClearCameraBinding(workspaceHost);
        }

        if (workspaceHost.IsActive(EditorWorkspaceId::Cinematics) &&
            !boundCameraObjectId_) {
            const std::optional<SceneObjectId> defaultCamera =
                scene.GetSceneDocument().camera.defaultCameraObjectId;
            if (defaultCamera &&
                IsEnabledCameraObject(scene, *defaultCamera)) {
                boundCameraObjectId_ = *defaultCamera;
                directorViewPanel_.SetTargetCamera(*defaultCamera);
                EditorViewInstance& gameView =
                    workspaceHost.GetCinematicsGameView();
                gameView.cameraBinding.kind =
                    EditorViewCameraSourceKind::SceneCameraObject;
                gameView.cameraBinding.sceneObjectId = *defaultCamera;
            }
        }

        sceneIdentity_ = currentIdentity;
        sceneDocumentRevision_ = currentRevision;
    }

    void CinematicsWorkspaceController::ApplyCameraOverviewAction(
        DocumentSceneBase& scene,
        const CameraOverviewAction& action,
        EditorContext& context,
        EditorWorkspaceHost& workspaceHost,
        CinematicsWorkspaceResult& result) {

        if (!action.IsValid()) {
            return;
        }

        switch (action.kind) {
        case CameraOverviewActionKind::Select:
            directorViewPanel_.SetTargetCamera(action.cameraObjectId);
            break;
        case CameraOverviewActionKind::SetDefault:
            directorViewPanel_.SetTargetCamera(action.cameraObjectId);
            if (scene.SetGameDefaultCamera(action.cameraObjectId)) {
                context.sceneDirty = true;
                scene.SetUnsavedSceneChanges(true);
            }
            break;
        case CameraOverviewActionKind::ViewThrough:
            cameraTimelinePanel_.SuspendPreview();
            timelinePreviewOwned_ = false;
            timelineRestoreCameraObjectId_.reset();
            directorViewPanel_.SetTargetCamera(action.cameraObjectId);
            if (scene.IsRuntimePlayActive()) {
                result.statusMessage =
                    "Camera preview is unavailable while Play is running";
                break;
            }
            if (BindCameraPreview(
                    scene,
                    action.cameraObjectId,
                    workspaceHost)) {
                cameraPreviewOwned_ = true;
            }
            break;
        case CameraOverviewActionKind::ExitView:
            cameraTimelinePanel_.SuspendPreview();
            timelinePreviewOwned_ = false;
            timelineRestoreCameraObjectId_.reset();
            scene.EndEditorCameraPreview();
            cameraPreviewOwned_ = false;
            preWorkspacePreviewObjectId_.reset();
            ClearCameraBinding(workspaceHost);
            break;
        case CameraOverviewActionKind::SnapToView:
            {
                const DirectorCameraPose pose = CameraPoseFromView(
                    directorViewPanel_.GetViewCamera());
                if (scene.ApplyCameraObjectPose(
                        action.cameraObjectId,
                        pose.position,
                        pose.rotation,
                        true)) {
                    directorViewPanel_.SetTargetCamera(action.cameraObjectId);
                    context.sceneDirty = true;
                    scene.SetUnsavedSceneChanges(true);
                }
            }
            break;
        default:
            break;
        }
    }

    void CinematicsWorkspaceController::ApplyDirectorViewResult(
        DocumentSceneBase& scene,
        const DirectorViewPanelResult& result,
        EditorContext& context,
        SelectionSyncService& selectionSync) {

        if (result.selectionRequested) {
            if (GameObject* object = FindRuntimeObject(
                    scene,
                    result.selectedObjectId)) {
                context.selection.selectedObject = object;
                context.selection.selectedAsset = nullptr;
                context.selection.selectedAssetGuid.clear();
                context.selection.selectedAssetPath.clear();
            }
        }

        const auto applyCameraPose = [&scene, &context](
            SceneObjectId cameraObjectId,
            const DirectorCameraPose& pose) {
            if (!scene.ApplyCameraObjectPose(
                    cameraObjectId,
                    pose.position,
                    pose.rotation,
                    true)) {
                return false;
            }
            context.sceneDirty = true;
            scene.SetUnsavedSceneChanges(true);
            return true;
        };

        if (result.alignCameraRequested) {
            (void)applyCameraPose(
                result.alignCameraObjectId,
                result.alignPose);
        }
        if (result.pilotPoseChanged) {
            (void)applyCameraPose(
                result.pilotCameraObjectId,
                result.pilotPose);
        }

        if (result.gizmo.changed) {
            if (GameObject* object = FindRuntimeObject(
                    scene,
                    result.gizmo.objectId)) {
                Transform3D& runtimeTransform = object->Transform();
                const bool cameraPoseApplied =
                    object->GetComponent<CameraComponent>() != nullptr &&
                    scene.ApplyCameraObjectPose(
                        result.gizmo.objectId,
                        result.gizmo.transform.position,
                        result.gizmo.rotation,
                        true);
                if (!cameraPoseApplied) {
                    runtimeTransform.position =
                        result.gizmo.transform.position;
                    runtimeTransform.rotation = MATH::NormalizeQ(
                        result.gizmo.rotation);
                }
                runtimeTransform.scale = cameraPoseApplied
                    ? MATH::Vec3{ 1.0f, 1.0f, 1.0f }
                    : result.gizmo.transform.scale;
                runtimeTransform.useExplicitMatrix = false;
                object->MarkRenderStateDirty();

                if (SceneObjectData* documentObject =
                        selectionSync.FindDocumentObjectByRuntime(
                            scene,
                            object)) {
                    const MATH::Vec3 rotationEulerDeg =
                        MATH::EulerXYZDegreesFromQuatNearest(
                            result.gizmo.rotation,
                            documentObject->transform.rotationEulerDeg);
                    documentObject->transform = result.gizmo.transform;
                    documentObject->transform.rotationEulerDeg =
                        rotationEulerDeg;
                    if (cameraPoseApplied) {
                        documentObject->transform.scale = {
                            1.0f,
                            1.0f,
                            1.0f
                        };
                    }
                }
                context.sceneDirty = true;
                scene.SetUnsavedSceneChanges(true);
            }
        }
        scene.SetViewportGizmoInteracting(result.gizmo.interacting);
    }

    void CinematicsWorkspaceController::ApplyCameraTimelineResult(
        DocumentSceneBase& scene,
        const CameraTimelinePanelResult& result,
        EditorContext& context,
        EditorWorkspaceHost& workspaceHost) {

        if (result.documentChanged) {
            context.sceneDirty = true;
            scene.SetUnsavedSceneChanges(true);
        }

        const bool canPreview =
            !scene.IsRuntimePlayActive() &&
            result.previewEnabled &&
            result.evaluation.IsValid() &&
            IsEnabledCameraObject(scene, result.evaluation.cameraObjectId);
        if (!canPreview) {
            RestoreTimelinePreview(scene, workspaceHost);
            return;
        }

        if (!timelinePreviewOwned_) {
            timelineRestoreCameraObjectId_ = boundCameraObjectId_;
        }
        if (BindCameraPreview(
                scene,
                result.evaluation.cameraObjectId,
                workspaceHost)) {
            timelinePreviewOwned_ = true;
        } else {
            RestoreTimelinePreview(scene, workspaceHost);
        }
    }

    bool CinematicsWorkspaceController::BindCameraPreview(
        DocumentSceneBase& scene,
        SceneObjectId cameraObjectId,
        EditorWorkspaceHost& workspaceHost) {

        if (scene.IsRuntimePlayActive() ||
            !IsEnabledCameraObject(scene, cameraObjectId)) {
            return false;
        }
        if (!scene.IsEditorCameraPreviewActive() ||
            !(scene.GetEditorCameraPreviewObjectId() == cameraObjectId)) {
            if (!scene.BeginEditorCameraPreview(cameraObjectId)) {
                return false;
            }
        }

        cameraPreviewOwned_ = true;
        boundCameraObjectId_ = cameraObjectId;
        directorViewPanel_.SetTargetCamera(cameraObjectId);
        EditorViewInstance& gameView =
            workspaceHost.GetCinematicsGameView();
        gameView.cameraBinding.kind =
            EditorViewCameraSourceKind::SceneCameraObject;
        gameView.cameraBinding.sceneObjectId = cameraObjectId;
        return true;
    }

    void CinematicsWorkspaceController::RestoreTimelinePreview(
        DocumentSceneBase& scene,
        EditorWorkspaceHost& workspaceHost) {

        if (!timelinePreviewOwned_) {
            return;
        }
        const std::optional<SceneObjectId> restoreCamera =
            timelineRestoreCameraObjectId_;
        timelinePreviewOwned_ = false;
        timelineRestoreCameraObjectId_.reset();

        if (scene.IsRuntimePlayActive()) {
            cameraPreviewOwned_ = false;
            return;
        }
        if (restoreCamera &&
            IsEnabledCameraObject(scene, *restoreCamera) &&
            BindCameraPreview(scene, *restoreCamera, workspaceHost)) {
            return;
        }
        if (cameraPreviewOwned_) {
            scene.EndEditorCameraPreview();
        }
        cameraPreviewOwned_ = false;
        ClearCameraBinding(workspaceHost);
    }

    void CinematicsWorkspaceController::ClearCameraBinding(
        EditorWorkspaceHost& workspaceHost) {

        boundCameraObjectId_.reset();
        EditorViewInstance& gameView =
            workspaceHost.GetCinematicsGameView();
        gameView.cameraBinding.kind =
            EditorViewCameraSourceKind::OwnedEditorCamera;
        gameView.cameraBinding.sceneObjectId = {};
    }

} // namespace HIKARI::EDITOR
