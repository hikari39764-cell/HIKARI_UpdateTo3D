#include "Editor/Workspaces/HIKARI_ModelCollisionWorkspaceController.h"

#include <algorithm>
#include "Core/HIKARI_TimeService.h"
#include "Editor/Views/HIKARI_EditorViewInputGate.h"
#include "Editor/Workspaces/HIKARI_EditorWorkspaceHost.h"
#include "Editor/Workspaces/HIKARI_ModelCollisionWorkspaceInteraction.h"
#include "Editor/Workspaces/HIKARI_ModelCollisionViewportOverlay.h"
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

namespace HIKARI::EDITOR {
    namespace {
#if defined(HIKARI_WITH_EDITOR)
        constexpr float kMinimumPreviewSize = 64.0f;

#endif
    }

    void ModelCollisionWorkspaceController::DrawDockSpace(
        bool resetDefaultDockLayout) const {
#if defined(HIKARI_WITH_EDITOR)
        ImGuiIO& io = ImGui::GetIO();
        if ((io.ConfigFlags & ImGuiConfigFlags_DockingEnable) == 0) {
            return;
        }
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImGuiID dockspaceId = ImGui::GetID(
            "HIKARI_ModelCollisionDockSpace_v1");
        const bool needsLayout =
            ImGui::DockBuilderGetNode(dockspaceId) == nullptr;
        if (needsLayout || resetDefaultDockLayout) {
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(
                dockspaceId,
                ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

            ImGuiID previewNode = dockspaceId;
            ImGuiID leftNode = 0;
            ImGuiID rightNode = 0;
            ImGuiID leftBottom = 0;
            ImGuiID rightBottom = 0;
            ImGui::DockBuilderSplitNode(
                previewNode, ImGuiDir_Left, 0.22f, &leftNode, &previewNode);
            ImGui::DockBuilderSplitNode(
                previewNode, ImGuiDir_Right, 0.27f, &rightNode, &previewNode);
            ImGui::DockBuilderSplitNode(
                leftNode, ImGuiDir_Down, 0.48f, &leftBottom, &leftNode);
            ImGui::DockBuilderSplitNode(
                rightNode, ImGuiDir_Down, 0.45f, &rightBottom, &rightNode);

            ImGui::DockBuilderDockWindow(
                "Model Preview###ModelCollision/Preview", previewNode);
            ImGui::DockBuilderDockWindow(
                "Collision Shapes###ModelCollision/Shapes", leftNode);
            ImGui::DockBuilderDockWindow(
                "Source Model###ModelCollision/Source", leftBottom);
            ImGui::DockBuilderDockWindow(
                "Shape Details###ModelCollision/Details", rightNode);
            ImGui::DockBuilderDockWindow(
                "Auto Generate###ModelCollision/Generate", rightBottom);
            ImGui::DockBuilderFinish(dockspaceId);
        }
        ImGui::DockSpaceOverViewport(dockspaceId, viewport);
#else
        (void)resetDefaultDockLayout;
#endif
    }

    ModelCollisionWorkspaceResult ModelCollisionWorkspaceController::Draw(
        DocumentSceneBase& scene,
        EditorWorkspaceHost& workspaceHost) {

        ModelCollisionWorkspaceResult result{};
        result.statusMessage = statusMessage_;
#if defined(HIKARI_WITH_EDITOR)
        DrawPreviewWindow(scene, workspaceHost, result);
        DrawShapeListWindow();
        DrawShapeDetailsWindow();
        DrawSourceModelWindow();
        DrawAutoGenerateWindow();
        DrawPendingModelOpenModal(scene, result);
        DrawPendingCloseModal(scene, result);
#else
        (void)scene;
        (void)workspaceHost;
#endif
        return result;
    }

    void ModelCollisionWorkspaceController::DrawPreviewWindow(
        DocumentSceneBase& scene,
        EditorWorkspaceHost& workspaceHost,
        ModelCollisionWorkspaceResult& result) {
#if defined(HIKARI_WITH_EDITOR)
        constexpr ImGuiWindowFlags previewWindowFlags =
            ImGuiWindowFlags_NoScrollbar |
            ImGuiWindowFlags_NoScrollWithMouse |
            ImGuiWindowFlags_NoCollapse;
        if (!ImGui::Begin(
                "Model Preview###ModelCollision/Preview",
                nullptr,
                previewWindowFlags)) {
            ImGui::End();
            return;
        }

        const std::string title = IsEditingModel()
            ? modelDisplayName_ + (history_.IsDirty() ? " *" : "")
            : std::string("No Model");
        ImGui::TextUnformatted(title.c_str());
        ImGui::SameLine();
        if (ImGui::Button("Save")) {
            (void)SaveDocument(scene, statusMessage_);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!history_.CanUndo());
        if (ImGui::Button("Undo")) {
            (void)Undo(statusMessage_);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!history_.CanRedo());
        if (ImGui::Button("Redo")) {
            (void)Redo(statusMessage_);
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Frame Selection")) {
            FocusSelection();
        }
        ImGui::SameLine();
        if (ImGui::Button("Frame Model")) {
            FitPreviewCamera();
        }
        ImGui::SameLine();
        ImGui::Checkbox("Model", &showModel_);
        ImGui::SameLine();
        ImGui::Checkbox("Collision", &showCollision_);
        ImGui::SameLine();
        if (ImGui::Button("Exit to Scene")) {
            if (history_.IsDirty()) {
                closeRequested_ = true;
            } else {
                result.exitToSceneRequested = true;
            }
        }

        ImGui::Separator();
        if (ImGui::RadioButton(
                "Edit Shapes",
                selectionMode_ ==
                    ModelCollisionSelectionMode::CollisionShapes)) {
            selectionMode_ = ModelCollisionSelectionMode::CollisionShapes;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(
                "Pick Source Parts",
                selectionMode_ ==
                    ModelCollisionSelectionMode::SourceNodes)) {
            selectionMode_ = ModelCollisionSelectionMode::SourceNodes;
        }
        ImGui::SameLine();
        ImGui::TextDisabled(
            selectionMode_ == ModelCollisionSelectionMode::CollisionShapes
                ? "Click shapes; Ctrl/Shift adds to selection"
                : "Click model parts; Ctrl/Shift adds to selection");
        ImGui::Separator();
        if (ImGui::RadioButton(
                "Move", gizmoState_.operation ==
                    EditorTransformGizmoOperation::Translate)) {
            gizmoState_.operation =
                EditorTransformGizmoOperation::Translate;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(
                "Rotate", gizmoState_.operation ==
                    EditorTransformGizmoOperation::Rotate)) {
            gizmoState_.operation =
                EditorTransformGizmoOperation::Rotate;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton(
                "Scale", gizmoState_.operation ==
                    EditorTransformGizmoOperation::Scale)) {
            gizmoState_.operation =
                EditorTransformGizmoOperation::Scale;
        }
        ImGui::SameLine();
        if (ImGui::Button(gizmoState_.mode ==
                EditorTransformGizmoMode::Local ? "Local" : "World")) {
            gizmoState_.mode = gizmoState_.mode ==
                    EditorTransformGizmoMode::Local
                ? EditorTransformGizmoMode::World
                : EditorTransformGizmoMode::Local;
        }
        ImGui::SameLine();
        ImGui::Checkbox("Snap", &gizmoState_.snapEnabled);
        ImGui::SameLine();
        ImGui::TextDisabled(
            "RMB+WASDQE fly | Alt+LMB orbit | MMB pan | Wheel zoom | F frame selection");

        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        canvasSize.x = (std::max)(canvasSize.x, kMinimumPreviewSize);
        canvasSize.y = (std::max)(canvasSize.y, kMinimumPreviewSize);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(
            origin,
            { origin.x + canvasSize.x, origin.y + canvasSize.y },
            IM_COL32(15, 19, 25, 255));

        EditorViewInstance& view =
            workspaceHost.GetModelCollisionPreviewView();
        view.extent.width = static_cast<uint32_t>(canvasSize.x);
        view.extent.height = static_cast<uint32_t>(canvasSize.y);
        const float aspect = (std::max)(
            0.05f,
            canvasSize.x / canvasSize.y);

        const RENDER3D::EDITORVIEW::EditorInteractiveViewOutput output =
            RENDER3D::EDITORVIEW::GetOutput(view.renderViewId);
        const bool outputReady = showModel_ && output.ready &&
            output.colorSrv.ptr != 0 &&
            output.sceneRevision == previewScene_.GetRevision();
        ImGui::SetCursorScreenPos(origin);
        if (outputReady) {
            ImGui::Image(
                reinterpret_cast<ImTextureID>(
                    static_cast<uintptr_t>(output.colorSrv.ptr)),
                canvasSize);
        } else {
            ImGui::InvisibleButton(
                "##ModelCollisionPreviewCanvas",
                canvasSize,
                ImGuiButtonFlags_MouseButtonLeft |
                    ImGuiButtonFlags_MouseButtonRight |
                    ImGuiButtonFlags_MouseButtonMiddle);
            if (showModel_ && IsEditingModel()) {
                drawList->AddText(
                    { origin.x + 14.0f, origin.y + 14.0f },
                    IM_COL32(180, 194, 207, 255),
                    "Preparing model preview...");
            }
        }

        const bool canvasVisible = ImGui::IsItemVisible();
        const bool hovered = ImGui::IsItemHovered();
        if (hovered) {
            ImGui::SetItemKeyOwner(
                ImGuiKey_MouseWheelY,
                ImGuiInputFlags_LockThisFrame);
            ImGui::SetItemKeyOwner(
                ImGuiKey_MouseWheelX,
                ImGuiInputFlags_LockThisFrame);
        }
        const bool focused = ImGui::IsWindowFocused(
            ImGuiFocusedFlags_RootAndChildWindows);
        const ImGuiIO& io = ImGui::GetIO();
        const EditorViewInputBlockState block =
            QueryEditorViewInputBlockState();
        EditorViewInputSubmission submission{};
        submission.rect = {
            origin.x, origin.y, canvasSize.x, canvasSize.y
        };
        submission.visible = canvasVisible;
        submission.focused = focused;
        submission.hovered = hovered;
        submission.rightMouseDown =
            ImGui::IsMouseDown(ImGuiMouseButton_Right);
        submission.rightMouseClicked =
            ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        submission.middleMouseDown =
            ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        submission.middleMouseClicked =
            ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
        submission.leftMouseDown =
            ImGui::IsMouseDown(ImGuiMouseButton_Left);
        submission.leftMouseClicked =
            ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        submission.altDown = io.KeyAlt;
        submission.pointerBlocked = block.pointer;
        submission.keyboardBlocked = block.keyboard;
        inputRouter_.SetGizmoCapture(view.renderViewId, false);
        const EditorViewInputState& input = inputRouter_.Submit(
            view.renderViewId,
            submission);

        EditorDirectorCameraInput cameraInput{};
        cameraInput.lookActive = input.rightMouseCaptured;
        cameraInput.orbitActive = input.orbitMouseCaptured;
        cameraInput.panActive = input.middleMouseCaptured;
        cameraInput.mouseDeltaX = io.MouseDelta.x;
        cameraInput.mouseDeltaY = io.MouseDelta.y;
        cameraInput.wheelDelta = input.AcceptsWheel()
            ? io.MouseWheel * (io.KeyShift ? 4.0f : 1.0f)
            : 0.0f;
        cameraInput.fast = io.KeyShift;
        if (input.rightMouseCaptured) {
            cameraInput.moveForward = ImGui::IsKeyDown(ImGuiKey_W);
            cameraInput.moveBackward = ImGui::IsKeyDown(ImGuiKey_S);
            cameraInput.moveRight = ImGui::IsKeyDown(ImGuiKey_D);
            cameraInput.moveLeft = ImGui::IsKeyDown(ImGuiKey_A);
            cameraInput.moveUp = ImGui::IsKeyDown(ImGuiKey_E);
            cameraInput.moveDown = ImGui::IsKeyDown(ImGuiKey_Q);
        }
        bool cameraChanged = cameraController_.Update(
            cameraInput,
            TIME::GetFrameContext().unscaledDt,
            aspect);
        if (input.AcceptsKeyboard() && hovered && !io.WantTextInput) {
            if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                FocusSelection();
            }
            if (ImGui::IsKeyPressed(ImGuiKey_W, false) &&
                !input.rightMouseCaptured) {
                gizmoState_.operation =
                    EditorTransformGizmoOperation::Translate;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_E, false) &&
                !input.rightMouseCaptured) {
                gizmoState_.operation =
                    EditorTransformGizmoOperation::Rotate;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_R, false) &&
                !input.rightMouseCaptured) {
                gizmoState_.operation =
                    EditorTransformGizmoOperation::Scale;
            }
            if (selectionMode_ ==
                    ModelCollisionSelectionMode::CollisionShapes &&
                ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
                DeleteSelectedShapes();
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
                (void)SaveDocument(scene, statusMessage_);
            }
            if (selectionMode_ ==
                    ModelCollisionSelectionMode::CollisionShapes &&
                io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D, false)) {
                DuplicateSelectedShapes();
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
                if (io.KeyShift) {
                    (void)Redo(statusMessage_);
                } else {
                    (void)Undo(statusMessage_);
                }
            }
            if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
                (void)Redo(statusMessage_);
            }
            if (selectionMode_ ==
                    ModelCollisionSelectionMode::CollisionShapes &&
                !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_H, false)) {
                SetSelectedShapesHidden(!AreAllSelectedShapesHidden());
            }
            if (selectionMode_ ==
                    ModelCollisionSelectionMode::CollisionShapes &&
                !io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_L, false)) {
                SetSelectedShapesLocked(!AreAllSelectedShapesLocked());
            }
        }
        if (cameraChanged) {
            ++cameraRevision_;
        }
        const Camera3D& camera = cameraController_.GetCamera();

        ModelCollisionPointerRay pointerRay{};
        const bool pointerRayValid = hovered && !block.pointer &&
            canvasSize.x > 0.0f && canvasSize.y > 0.0f &&
            BuildModelCollisionPointerRay(
                camera,
                (io.MousePos.x - origin.x) / canvasSize.x,
                (io.MousePos.y - origin.y) / canvasSize.y,
                pointerRay);
        hoveredShapeId_ = 0u;
        hoveredSourceNodeIndex_ = -1;
        if (pointerRayValid &&
            !input.rightMouseCaptured &&
            !input.middleMouseCaptured &&
            !input.orbitMouseCaptured) {
            if (selectionMode_ ==
                    ModelCollisionSelectionMode::CollisionShapes &&
                showCollision_) {
                hoveredShapeId_ = PickModelCollisionShape(
                    pointerRay,
                    setup_,
                    hiddenShapeIds_,
                    showGeneratedOnly_);
            } else if (selectionMode_ ==
                    ModelCollisionSelectionMode::SourceNodes &&
                showModel_) {
                hoveredSourceNodeIndex_ = PickModelCollisionSourceNode(
                    pointerRay,
                    previewScene_.GetSourceNodes());
            }
        }

        if (showCollision_) {
            for (const ASSETS::COLLISION::ModelCollisionShape& shape :
                    setup_.shapes) {
                if (showGeneratedOnly_ && !shape.generated) {
                    continue;
                }
                if (IsShapeHidden(shape.id)) {
                    continue;
                }
                DrawModelCollisionShapeOverlay(
                    drawList,
                    camera,
                    origin.x,
                    origin.y,
                    canvasSize.x,
                    canvasSize.y,
                    shape,
                    IsShapeSelected(shape.id),
                    shape.id == hoveredShapeId_,
                    IsShapeLocked(shape.id));
            }
        }

        if (showModel_) {
            const ModelAsset* previewModel = previewScene_.GetModel();
            size_t highlightedNodeCount = 0u;
            for (const ModelCollisionPreviewNode& node :
                previewScene_.GetSourceNodes()) {
                const bool selectedNode =
                    selectedSourceNodes_.contains(node.nodeIndex);
                const bool hoveredNode =
                    node.nodeIndex == hoveredSourceNodeIndex_;
                if (!selectedNode && !hoveredNode) {
                    continue;
                }
                const uint32_t highlightColor = hoveredNode
                    ? IM_COL32(255, 210, 92, 255)
                    : IM_COL32(76, 205, 255, 245);
                const float highlightThickness = hoveredNode
                    ? 2.4f
                    : 1.9f;
                const bool drewGeometryOutline = previewModel != nullptr &&
                    sourceOutlineCache_.Draw(
                        drawList,
                        camera,
                        origin.x,
                        origin.y,
                        canvasSize.x,
                        canvasSize.y,
                        *previewModel,
                        node,
                        previewScene_.GetRevision(),
                        highlightColor,
                        highlightThickness);
                DrawModelCollisionBoundsOverlay(
                    drawList,
                    camera,
                    origin.x,
                    origin.y,
                    canvasSize.x,
                    canvasSize.y,
                    node.bounds,
                    drewGeometryOutline
                        ? (hoveredNode
                            ? IM_COL32(255, 210, 92, 105)
                            : IM_COL32(76, 205, 255, 85))
                        : highlightColor,
                    drewGeometryOutline ? 1.0f : highlightThickness);
                ++highlightedNodeCount;
                if (highlightedNodeCount >= 1024u) {
                    break;
                }
            }
            if (hoveredSourceNodeIndex_ >= 0) {
                const ModelCollisionPreviewNode* hoveredNode =
                    previewScene_.FindSourceNode(hoveredSourceNodeIndex_);
                if (hoveredNode != nullptr) {
                    drawList->AddText(
                        { io.MousePos.x + 14.0f, io.MousePos.y + 12.0f },
                        IM_COL32(255, 225, 145, 255),
                        hoveredNode->name.c_str());
                }
            }
        }

        ASSETS::COLLISION::ModelCollisionShape* selected =
            SelectedShape();
        EditorTransformGizmoResult gizmo{};
        if (selectionMode_ ==
                ModelCollisionSelectionMode::CollisionShapes &&
            selected != nullptr && showCollision_ && selected->enabled &&
            !IsShapeHidden(selected->id) &&
            !IsShapeLocked(selected->id) &&
            !block.pointer) {
            gizmo = transformGizmo_.DrawTransform(
                BuildModelCollisionShapeTransform(*selected),
                camera,
                gizmoState_,
                { origin.x, origin.y, canvasSize.x, canvasSize.y });
            if (gizmo.changed) {
                if (!gizmoEditActive_) {
                    gizmoEditActive_ = true;
                    gizmoEditChanged_ = false;
                }
                selected->center = gizmo.transform.position;
                selected->rotationEulerDegrees =
                    gizmo.transform.rotationEulerDeg;
                const MATH::Vec3 scale = gizmo.transform.scale;
                if (selected->type ==
                        ASSETS::COLLISION::CollisionGeometryShapeType::Box) {
                    selected->size = scale;
                } else if (selected->type ==
                        ASSETS::COLLISION::CollisionGeometryShapeType::Sphere) {
                    selected->radius = (std::max)(
                        0.001f,
                        (scale.x + scale.y + scale.z) / 6.0f);
                } else if (selected->type == ASSETS::COLLISION::
                        CollisionGeometryShapeType::Capsule) {
                    selected->radius = (std::max)(
                        0.001f,
                        (scale.x + scale.z) * 0.25f);
                    selected->height = (std::max)(
                        scale.y,
                        selected->radius * 2.0f);
                }
                selected->generated = false;
                selected->sourceNodeIndices.clear();
                selected->generationMethod.clear();
                gizmoEditChanged_ = true;
            }
        }
        inputRouter_.SetGizmoCapture(
            view.renderViewId,
            gizmo.interacting);
        if (gizmoEditActive_ && !gizmo.interacting) {
            if (gizmoEditChanged_) {
                CommitEdit("Transform Collision Shape");
            }
            gizmoEditActive_ = false;
            gizmoEditChanged_ = false;
        }

        if (pointerRayValid &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !io.KeyAlt && !gizmo.interacting) {
            const bool additive = io.KeyCtrl || io.KeyShift;
            if (selectionMode_ ==
                    ModelCollisionSelectionMode::CollisionShapes) {
                SelectShape(hoveredShapeId_, additive);
            } else {
                SelectSourceNode(hoveredSourceNodeIndex_, additive);
                if (hoveredSourceNodeIndex_ >= 0) {
                    sourceSearch_[0] = '\0';
                }
            }
        }

        if (showModel_ && previewScene_.IsReady()) {
            RENDER3D::EDITORVIEW::EditorInteractiveViewRequest request{};
            request.viewId = view.renderViewId;
            request.cameraFrame.camera = camera;
            request.cameraFrame.revision = cameraRevision_;
            request.cameraFrame.cameraCut = cameraCutPending_;
            request.cameraFrame.valid = true;
            request.width = view.extent.width;
            request.height = view.extent.height;
            request.sceneRevision = previewScene_.GetRevision();
            request.visible = canvasVisible;
            request.drawDebug = false;
            request.shadingMode =
                RENDER3D::EDITORVIEW::EditorInteractiveShadingMode::Neutral;
            request.displayExposure = 1.0f;
            request.sceneSourceOverride = previewScene_.GetSceneSource();
            RENDER3D::EDITORVIEW::SubmitRequest(request);
        } else {
            RENDER3D::EDITORVIEW::ClearRequest(view.renderViewId);
        }
        cameraCutPending_ = false;

        ImGui::SetCursorScreenPos({ origin.x, origin.y + canvasSize.y });
        ImGui::Dummy(ImVec2(1.0f, 1.0f));
        if (!statusMessage_.empty()) {
            ImGui::TextWrapped("%s", statusMessage_.c_str());
        }
        ImGui::End();
#else
        (void)scene;
        (void)workspaceHost;
        (void)result;
#endif
    }

} // namespace HIKARI::EDITOR
