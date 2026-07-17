#include "Editor/Workspaces/HIKARI_CinematicsWorkspaceController.h"

#include <algorithm>

#include "Editor/HIKARI_EditorContext.h"
#include "Editor/HIKARI_EditorViewportInput.h"
#include "Editor/HIKARI_SelectionSyncService.h"
#include "Editor/Play/HIKARI_EditorPlaySession.h"
#include "HIKARI_Services.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI::EDITOR {

    namespace {
#if defined(HIKARI_WITH_EDITOR)
        constexpr float kTimelineHeightRatio = 0.25f;
        constexpr float kRightColumnWidthRatio = 0.30f;
        constexpr float kCameraPanelHeightRatio = 0.60f;

        bool CanUseViewportShortcut(bool focused) {
            if (!focused) {
                return false;
            }
            ImGuiIO& io = ImGui::GetIO();
            if (io.WantTextInput || ImGui::IsAnyItemActive() ||
                ImGui::GetDragDropPayload() != nullptr) {
                return false;
            }
            if (ImGui::IsPopupOpen(
                    nullptr,
                    ImGuiPopupFlags_AnyPopupId)) {
                return false;
            }
            return !ImGui::IsMouseDown(ImGuiMouseButton_Right);
        }

        void HandleTransformGizmoShortcuts(
            EditorTransformGizmoState& state,
            bool focused) {

            if (!CanUseViewportShortcut(focused)) {
                return;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_Q) ||
                ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                state.enabled = false;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_W)) {
                state.enabled = true;
                state.operation = EditorTransformGizmoOperation::Translate;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_E)) {
                state.enabled = true;
                state.operation = EditorTransformGizmoOperation::Rotate;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_R)) {
                state.enabled = true;
                state.operation = EditorTransformGizmoOperation::Scale;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_X)) {
                state.mode = state.mode == EditorTransformGizmoMode::World
                    ? EditorTransformGizmoMode::Local
                    : EditorTransformGizmoMode::World;
            }
        }
#endif
    }

    void CinematicsWorkspaceController::DrawDockSpace(
        bool resetDefaultDockLayout) const {
#if defined(HIKARI_WITH_EDITOR)
        ImGuiIO& io = ImGui::GetIO();
        if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0) {
            return;
        }

        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImGuiID dockspaceId =
            ImGui::GetID("HIKARI_CinematicsDockSpace_v4");
        const ImGuiDockNodeFlags dockspaceFlags = ImGuiDockNodeFlags_None;
        const bool needsDefaultLayout =
            ImGui::DockBuilderGetNode(dockspaceId) == nullptr;
        if (needsDefaultLayout || resetDefaultDockLayout) {
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(
                dockspaceId,
                ImGuiDockNodeFlags_DockSpace | dockspaceFlags);
            ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

            ImGuiID contentNode = dockspaceId;
            ImGuiID timelineNode = 0;
            ImGui::DockBuilderSplitNode(
                contentNode,
                ImGuiDir_Down,
                kTimelineHeightRatio,
                &timelineNode,
                &contentNode);

            ImGuiID directorNode = contentNode;
            ImGuiID rightNode = 0;
            ImGuiID gameNode = 0;
            ImGuiID cameraNode = 0;
            ImGui::DockBuilderSplitNode(
                directorNode,
                ImGuiDir_Right,
                kRightColumnWidthRatio,
                &rightNode,
                &directorNode);
            ImGui::DockBuilderSplitNode(
                rightNode,
                ImGuiDir_Down,
                kCameraPanelHeightRatio,
                &cameraNode,
                &gameNode);

            ImGui::DockBuilderDockWindow(
                "Director View###Cinematics/DirectorView",
                directorNode);
            ImGui::DockBuilderDockWindow(
                "Game View###Cinematics/GameView",
                gameNode);
            ImGui::DockBuilderDockWindow(
                "Map Overview###Cinematics/MapOverview",
                cameraNode);
            ImGui::DockBuilderDockWindow(
                "Cameras###Cinematics/Cameras",
                cameraNode);
            ImGui::DockBuilderDockWindow(
                "Timeline###Cinematics/Timeline",
                timelineNode);
            ImGui::DockBuilderFinish(dockspaceId);
        }
        ImGui::DockSpaceOverViewport(
            dockspaceId,
            viewport,
            dockspaceFlags);
#else
        (void)resetDefaultDockLayout;
#endif
    }

    CinematicsWorkspaceResult CinematicsWorkspaceController::Draw(
        DocumentSceneBase& scene,
        EditorPlaySession& playSession,
        EditorContext& context,
        SelectionSyncService& selectionSync,
        EditorWorkspaceHost& workspaceHost) {

        CinematicsWorkspaceResult result{};
        result.statusMessage.swap(pendingStatusMessage_);
#if defined(HIKARI_WITH_EDITOR)
        EditorViewInstance& gameView =
            workspaceHost.GetCinematicsGameView();
        if (scene.IsRuntimePlayActive()) {
            gameView.purpose = RENDER3D::RenderViewPurpose::Game;
            gameView.cameraBinding.kind =
                EditorViewCameraSourceKind::SceneDirector;
            gameView.cameraBinding.sceneObjectId = {};
        } else {
            gameView.purpose = RENDER3D::RenderViewPurpose::EditorScene;
            Camera3D resolvedBoundCamera{};
            if (boundCameraObjectId_ &&
                !scene.TryResolveCameraObjectView(
                    *boundCameraObjectId_,
                    16.0f / 9.0f,
                    resolvedBoundCamera)) {
                if (cameraPreviewOwned_) {
                    scene.EndEditorCameraPreview();
                }
                cameraPreviewOwned_ = false;
                ClearCameraBinding(workspaceHost);
            }

            if (boundCameraObjectId_) {
                gameView.cameraBinding.kind =
                    EditorViewCameraSourceKind::SceneCameraObject;
                gameView.cameraBinding.sceneObjectId =
                    *boundCameraObjectId_;
                if (!scene.IsEditorCameraPreviewActive()) {
                    cameraPreviewOwned_ = scene.BeginEditorCameraPreview(
                        *boundCameraObjectId_);
                    if (!cameraPreviewOwned_) {
                        ClearCameraBinding(workspaceHost);
                    }
                }
            } else {
                gameView.cameraBinding.kind =
                    EditorViewCameraSourceKind::OwnedEditorCamera;
                gameView.cameraBinding.sceneObjectId = {};
            }
        }

        DrawTimelineWindow(scene, context, workspaceHost, result);
        DrawDirectorViewWindow(
            scene,
            context,
            selectionSync,
            workspaceHost);
        result.toggleGamePreviewRequested =
            DrawGameViewWindow(scene, playSession, workspaceHost);
        DrawMapOverviewWindow(
            scene,
            context,
            workspaceHost,
            result);
        DrawCameraListWindow(
            scene,
            context,
            workspaceHost,
            result);
#else
        (void)scene;
        (void)playSession;
        (void)context;
        (void)selectionSync;
        (void)workspaceHost;
#endif
        return result;
    }

    void CinematicsWorkspaceController::DrawDirectorViewWindow(
        DocumentSceneBase& scene,
        EditorContext& context,
        SelectionSyncService& selectionSync,
        EditorWorkspaceHost& workspaceHost) {
#if defined(HIKARI_WITH_EDITOR)
        EditorViewInstance& view =
            workspaceHost.GetCinematicsDirectorView();
        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoCollapse;
        if (!ImGui::Begin(
                "Director View###Cinematics/DirectorView",
                nullptr,
                flags)) {
            view.extent = {};
            view.interaction = {};
            RENDER3D::EDITORVIEW::ClearRequest(view.renderViewId);
            ImGui::End();
            return;
        }

        const bool focused = ImGui::IsWindowFocused(
            ImGuiFocusedFlags_RootAndChildWindows);
        HandleTransformGizmoShortcuts(context.transformGizmo, focused);
        const SceneObjectId selectedObjectId =
            context.selection.selectedObject
                ? context.selection.selectedObject->GetDocumentId()
                : SceneObjectId{};
        const DirectorViewPanelResult result =
            directorViewPanel_.DrawContents(
                scene,
                view,
                selectedObjectId,
                context.transformGizmo,
                TIME::GetFrameContext().unscaledDt,
                scene.IsRuntimePlayActive());
        ApplyDirectorViewResult(
            scene,
            result,
            context,
            selectionSync);
        ImGui::End();
#else
        (void)scene;
        (void)context;
        (void)selectionSync;
        (void)workspaceHost;
#endif
    }

    bool CinematicsWorkspaceController::DrawGameViewWindow(
        DocumentSceneBase& scene,
        EditorPlaySession& playSession,
        EditorWorkspaceHost& workspaceHost) {

        bool toggleRequested = false;
#if defined(HIKARI_WITH_EDITOR)
        EditorViewInstance& view = workspaceHost.GetCinematicsGameView();
        constexpr ImGuiWindowFlags flags =
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoCollapse;

        ImGui::PushStyleVar(
            ImGuiStyleVar_WindowPadding,
            ImVec2(0.0f, 0.0f));
        if (!ImGui::Begin(
                "Game View###Cinematics/GameView",
                nullptr,
                flags)) {
            view.extent = {};
            view.interaction = {};
            ClearGameViewportInputRect();
            SERVICES::SetEditorGameViewportSize(0, 0, false);
            ImGui::End();
            ImGui::PopStyleVar();
            return false;
        }

        view.interaction.visible = true;
        constexpr float toolbarHeight = 36.0f;
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(0.055f, 0.065f, 0.080f, 1.0f));
        if (ImGui::BeginChild(
                "##CinematicsGameToolbar",
                ImVec2(0.0f, toolbarHeight),
                false,
                ImGuiWindowFlags_NoScrollbar)) {
            ImGui::SetCursorPos(ImVec2(8.0f, 7.0f));
            const bool previewRunning = playSession.IsRunning();
            if (ImGui::Button(previewRunning ? "Stop" : "Play")) {
                toggleRequested = true;
            }
            ImGui::SameLine();
            ImGui::TextUnformatted("Primary Full Quality");
            ImGui::SameLine();
            ImGui::TextDisabled(
                "%s",
                scene.IsRuntimePlayActive()
                    ? "Scene Director"
                    : (boundCameraObjectId_
                        ? "Camera Preview"
                        : "Editor Camera"));
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        canvasSize.x = (std::max)(canvasSize.x, 1.0f);
        canvasSize.y = (std::max)(canvasSize.y, 1.0f);
        const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
        constexpr float gameViewAspect = 16.0f / 9.0f;
        const EditorViewportFit viewportFit = FitEditorViewport(
            canvasSize.x,
            canvasSize.y,
            gameViewAspect);
        const ImVec2 imageSize{ viewportFit.width, viewportFit.height };
        const ImVec2 imageOrigin{
            canvasOrigin.x + viewportFit.offsetX,
            canvasOrigin.y + viewportFit.offsetY
        };
        ImGui::GetWindowDrawList()->AddRectFilled(
            canvasOrigin,
            { canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y },
            IM_COL32(4, 6, 9, 255));
        ImGui::SetCursorScreenPos(imageOrigin);
        const bool focused = ImGui::IsWindowFocused(
            ImGuiFocusedFlags_RootAndChildWindows);
        view.extent.width = static_cast<uint32_t>(imageSize.x + 0.5f);
        view.extent.height = static_cast<uint32_t>(imageSize.y + 0.5f);
        view.interaction.focused = focused;
        view.interaction.keyboardActive = focused;
        SERVICES::SetEditorGameViewportSize(
            static_cast<int>(view.extent.width),
            static_cast<int>(view.extent.height),
            true);

        const bool ready = POST::PostSystem::IsEditorViewportReady();
        const D3D12_GPU_DESCRIPTOR_HANDLE viewportSrv =
            POST::PostSystem::GetEditorViewportSrv();
        if (ready && viewportSrv.ptr != 0) {
            const ImTextureID textureId = reinterpret_cast<ImTextureID>(
                static_cast<uintptr_t>(viewportSrv.ptr));
            ImGui::Image(textureId, imageSize);
        } else {
            const ImVec2 imageMax{
                imageOrigin.x + imageSize.x,
                imageOrigin.y + imageSize.y
            };
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(
                imageOrigin,
                imageMax,
                IM_COL32(8, 10, 13, 255));
            drawList->AddText(
                ImVec2(imageOrigin.x + 16.0f, imageOrigin.y + 16.0f),
                IM_COL32(190, 205, 215, 255),
                "Waiting for primary render output");
            ImGui::Dummy(imageSize);
        }

        const bool hovered = ImGui::IsItemHovered();
        view.interaction.hovered = hovered;
        view.interaction.mouseCaptured = hovered &&
            ImGui::IsMouseDown(ImGuiMouseButton_Right);
        SetGameViewportInputRect(
            imageOrigin.x,
            imageOrigin.y,
            imageSize.x,
            imageSize.y,
            focused);
        ImGui::SetCursorScreenPos({
            canvasOrigin.x,
            canvasOrigin.y + canvasSize.y
        });
        ImGui::Dummy(ImVec2(1.0f, 1.0f));
        ImGui::End();
        ImGui::PopStyleVar();
#else
        (void)scene;
        (void)playSession;
        (void)workspaceHost;
#endif
        return toggleRequested;
    }

    void CinematicsWorkspaceController::DrawMapOverviewWindow(
        DocumentSceneBase& scene,
        EditorContext& context,
        EditorWorkspaceHost& workspaceHost,
        CinematicsWorkspaceResult& result) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin(
                "Map Overview###Cinematics/MapOverview",
                nullptr,
                ImGuiWindowFlags_NoCollapse)) {
            EditorViewInstance& view =
                workspaceHost.GetCinematicsOverviewView();
            view.extent = {};
            view.interaction = {};
            ImGui::End();
            return;
        }
        const CameraOverviewPanelResult panelResult =
            cameraOverviewPanel_.DrawOverviewContents(
                scene,
                context,
                workspaceHost.GetCinematicsOverviewView());
        ApplyCameraOverviewAction(
            scene,
            panelResult.action,
            context,
            workspaceHost,
            result);
        ImGui::End();
#else
        (void)scene;
        (void)context;
        (void)workspaceHost;
        (void)result;
#endif
    }

    void CinematicsWorkspaceController::DrawCameraListWindow(
        DocumentSceneBase& scene,
        EditorContext& context,
        EditorWorkspaceHost& workspaceHost,
        CinematicsWorkspaceResult& result) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin(
                "Cameras###Cinematics/Cameras",
                nullptr,
                ImGuiWindowFlags_NoCollapse)) {
            ImGui::End();
            return;
        }
        ImGui::TextUnformatted("Cinematics Workspace");
        ImGui::TextDisabled(
            "Select a camera here, then use Look Through or Pilot in Director.");
        ImGui::Separator();
        const CameraOverviewPanelResult panelResult =
            cameraOverviewPanel_.DrawCameraListContents(scene, context);
        ApplyCameraOverviewAction(
            scene,
            panelResult.action,
            context,
            workspaceHost,
            result);
        ImGui::End();
#else
        (void)scene;
        (void)context;
        (void)workspaceHost;
        (void)result;
#endif
    }

    void CinematicsWorkspaceController::DrawTimelineWindow(
        DocumentSceneBase& scene,
        EditorContext& context,
        EditorWorkspaceHost& workspaceHost,
        CinematicsWorkspaceResult& result) {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin(
                "Timeline###Cinematics/Timeline",
                nullptr,
                ImGuiWindowFlags_NoCollapse)) {
            ImGui::End();
            return;
        }

        AssetDatabase& assetDatabase = scene.GetAssetDatabase();
        SequenceEditorDocument& sequenceDocument =
            sequenceDocumentController_.GetDocument();
        SceneCinematicsSettings& initialSettings =
            sequenceDocument.GetSettings(
                scene.GetSceneDocument().cinematics);
        CinematicSequence* activeSequence =
            cameraTimelinePanel_.GetActiveSequence(initialSettings);
        const bool actionsAllowed = !scene.IsRuntimePlayActive();
        const SequenceEditorDocumentToolbarResult toolbarResult =
            sequenceDocumentToolbar_.Draw(
                sequenceDocumentController_,
                assetDatabase,
                activeSequence,
                scene.GetCurrentSceneDisplayName(),
                sequenceLibraryVisible_,
                actionsAllowed);
        if (toolbarResult.toggleLibraryRequested) {
            sequenceLibraryVisible_ = !sequenceLibraryVisible_;
        }
        result.saveSceneRequested |=
            toolbarResult.saveEmbeddedSceneRequested;
        if (toolbarResult.revealAssetRequested) {
            context.selection.selectedObject = nullptr;
            context.selection.selectedAsset = nullptr;
            context.selection.selectedAssetGuid =
                toolbarResult.revealAssetGuid.value;
            context.selection.selectedAssetPath =
                toolbarResult.revealAssetPath.generic_string();
            sequenceLibraryPanel_.Select(
                toolbarResult.revealAssetGuid);
        }
        if (!toolbarResult.statusMessage.empty()) {
            result.statusMessage = toolbarResult.statusMessage;
        }
        ImGui::Separator();

        if (sequenceDocumentController_.ConsumeTimelineResetRequested()) {
            cameraTimelinePanel_.ResetForScene();
        }

        if (sequenceLibraryVisible_) {
            ImGui::BeginChild(
                "##CinematicsSequenceLibrary",
                ImVec2(300.0f, 0.0f),
                ImGuiChildFlags_Border | ImGuiChildFlags_ResizeX);
            const SequenceLibraryPanelResult libraryResult =
                sequenceLibraryPanel_.Draw(
                    assetDatabase,
                    sequenceDocument.GetAssetGuid(),
                    actionsAllowed);
            ImGui::EndChild();
            if (libraryResult.action ==
                    SequenceLibraryActionKind::NewAsset) {
                (void)sequenceDocumentController_.RequestNewAsset(
                    assetDatabase,
                    result.statusMessage);
            } else if (libraryResult.action ==
                    SequenceLibraryActionKind::OpenAsset) {
                (void)RequestOpenSequenceAsset(
                    scene,
                    libraryResult.assetGuid,
                    result.statusMessage);
            }
            ImGui::SameLine();
        }

        ImGui::BeginChild(
            "##CinematicsSequenceTimeline",
            ImVec2(0.0f, 0.0f),
            ImGuiChildFlags_None);
        if (sequenceDocumentController_.ConsumeTimelineResetRequested()) {
            cameraTimelinePanel_.ResetForScene();
        }
        SceneCinematicsSettings& editableSettings =
            sequenceDocument.GetSettings(
                scene.GetSceneDocument().cinematics);
        const bool editingEmbedded = sequenceDocument.IsEmbeddedScene();
        std::optional<SceneCinematicsSettings> assetBefore{};
        if (!editingEmbedded && actionsAllowed) {
            assetBefore = editableSettings;
        }
        SceneObjectId selectedCameraObjectId{};
        if (context.selection.selectedObject != nullptr &&
            context.selection.selectedObject->GetComponent<CameraComponent>() !=
                nullptr) {
            selectedCameraObjectId =
                context.selection.selectedObject->GetDocumentId();
        }
        const CameraTimelinePanelResult panelResult =
            cameraTimelinePanel_.Draw(
                scene.GetSceneDocument(),
                editableSettings,
                sequenceDocument.GetAssetGuid(),
                selectedCameraObjectId,
                ImGui::GetIO().DeltaTime,
                actionsAllowed,
                editingEmbedded);
        if (editingEmbedded) {
            result.cinematicsChanged |= panelResult.documentChanged;
            result.timelineEditMergeId = panelResult.editMergeId;
        } else if (panelResult.documentChanged && assetBefore) {
            sequenceDocument.RecordApplied(
                std::move(*assetBefore),
                panelResult.editMergeId);
        } else if (panelResult.editMergeId == 0) {
            sequenceDocument.SealMerge();
        }
        ApplyCameraTimelineResult(
            scene,
            panelResult,
            context,
            workspaceHost,
            editingEmbedded);
        ImGui::EndChild();

        sequenceDocumentToolbar_.DrawPendingConfirmation(
            sequenceDocumentController_,
            assetDatabase,
            result.statusMessage);
        ImGui::End();
#else
        (void)scene;
        (void)context;
        (void)workspaceHost;
        (void)result;
#endif
    }

} // namespace HIKARI::EDITOR
