#include "Editor/Views/HIKARI_DirectorViewPanel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include "imgui_internal.h"
#endif

#if defined(HIKARI_WITH_EDITOR)
#include "Render3D/Views/HIKARI_EditorInteractiveViewRenderer.h"
#endif
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"

namespace HIKARI::EDITOR {

#if defined(HIKARI_WITH_EDITOR)
    namespace {
        constexpr float kMinimumCanvasSize = 64.0f;
        constexpr float kCameraHitDistance = 7.0f;
        constexpr float kRadiansToDegrees = 57.2957795131f;
        constexpr float kDirectorViewportAspect = 4.0f / 3.0f;

        struct CameraOverlayEntry {
            SceneObjectId objectId{};
            std::string name{};
            std::array<MATH::Vec3, 9> points{};
            bool enabled = false;
            bool selected = false;
        };

        const GameObject* FindRuntimeObject(
            const DocumentSceneBase& scene,
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

        bool HasDocumentParent(
            const DocumentSceneBase& scene,
            SceneObjectId objectId) {

            for (const SceneObjectData& object : scene.GetSceneDocument().objects) {
                if (object.id == objectId) {
                    return object.parent.has_value();
                }
            }
            return false;
        }

        bool IsEnabledCamera(
            const DocumentSceneBase& scene,
            SceneObjectId objectId) {

            const GameObject* object = FindRuntimeObject(scene, objectId);
            const CameraComponent* camera = object
                ? object->GetComponent<CameraComponent>()
                : nullptr;
            return camera != nullptr && camera->IsEnabled();
        }

        MATH::Vec3 ExtractPosition(const MATH::Mat4& matrix) {
            return { matrix.m[3][0], matrix.m[3][1], matrix.m[3][2] };
        }

        struct CameraOverlayBasis {
            MATH::Vec3 position{};
            MATH::Vec3 right{ 1.0f, 0.0f, 0.0f };
            MATH::Vec3 up{ 0.0f, 1.0f, 0.0f };
            MATH::Vec3 forward{ 0.0f, 0.0f, 1.0f };
        };

        CameraOverlayBasis BuildCameraOverlayBasis(const MATH::Mat4& world) {
            constexpr float epsilon = 1.0e-5f;

            CameraOverlayBasis basis{};
            basis.position = ExtractPosition(world);
            basis.forward = MATH::Normalize({
                world.m[2][0],
                world.m[2][1],
                world.m[2][2]
            });
            if (MATH::Length(basis.forward) <= epsilon) {
                basis.forward = { 0.0f, 0.0f, 1.0f };
            }

            MATH::Vec3 upCandidate = MATH::Normalize({
                world.m[1][0],
                world.m[1][1],
                world.m[1][2]
            });
            if (MATH::Length(upCandidate) <= epsilon ||
                std::abs(MATH::Dot(upCandidate, basis.forward)) >= 0.999f) {
                upCandidate = { 0.0f, 1.0f, 0.0f };
            }
            if (std::abs(MATH::Dot(upCandidate, basis.forward)) >= 0.999f) {
                upCandidate = { 1.0f, 0.0f, 0.0f };
            }

            basis.right = MATH::Normalize(MATH::Cross(
                upCandidate,
                basis.forward));
            basis.up = MATH::Normalize(MATH::Cross(
                basis.forward,
                basis.right));
            return basis;
        }

        MATH::Vec3 TransformCameraOverlayPoint(
            const CameraOverlayBasis& basis,
            float x,
            float y,
            float z) {

            return basis.position +
                basis.right * x +
                basis.up * y +
                basis.forward * z;
        }

        bool ProjectWorldToViewport(
            const Camera3D& camera,
            const MATH::Vec3& worldPosition,
            const ImVec2& viewportOrigin,
            const ImVec2& viewportSize,
            ImVec2& outScreenPosition) {

            const MATH::Vec4 clip = camera.GetViewProj().TransformPoint({
                worldPosition.x,
                worldPosition.y,
                worldPosition.z,
                1.0f
            });
            if (std::abs(clip.w) <= 1.0e-5f) {
                return false;
            }

            const float inverseW = 1.0f / clip.w;
            const float ndcX = clip.x * inverseW;
            const float ndcY = clip.y * inverseW;
            const float ndcZ = clip.z * inverseW;
            if (ndcZ < 0.0f || ndcZ > 1.0f) {
                return false;
            }

            outScreenPosition = {
                viewportOrigin.x + (ndcX * 0.5f + 0.5f) * viewportSize.x,
                viewportOrigin.y + (-ndcY * 0.5f + 0.5f) * viewportSize.y
            };
            return true;
        }

        float DistanceSquaredToSegment(
            const ImVec2& point,
            const ImVec2& start,
            const ImVec2& end) {

            const float segmentX = end.x - start.x;
            const float segmentY = end.y - start.y;
            const float lengthSquared =
                segmentX * segmentX + segmentY * segmentY;
            if (lengthSquared <= 1.0e-5f) {
                const float dx = point.x - start.x;
                const float dy = point.y - start.y;
                return dx * dx + dy * dy;
            }
            const float amount = std::clamp(
                ((point.x - start.x) * segmentX +
                    (point.y - start.y) * segmentY) / lengthSquared,
                0.0f,
                1.0f);
            const float nearestX = start.x + segmentX * amount;
            const float nearestY = start.y + segmentY * amount;
            const float dx = point.x - nearestX;
            const float dy = point.y - nearestY;
            return dx * dx + dy * dy;
        }

        std::vector<CameraOverlayEntry> GatherCameraOverlays(
            const DocumentSceneBase& scene,
            SceneObjectId selectedObjectId,
            float aspect,
            float overlayScale) {

            std::vector<CameraOverlayEntry> result{};
            const float safeOverlayScale = std::clamp(
                overlayScale,
                0.5f,
                2.0f);
            for (const auto& object : scene.GetWorld().GetObjects()) {
                if (!object) {
                    continue;
                }
                const CameraComponent* camera =
                    object->GetComponent<CameraComponent>();
                if (!camera) {
                    continue;
                }

                const float nearDistance =
                    camera->GetNearClip() * safeOverlayScale;
                const float farDistance = (std::min)(
                    camera->GetFarClip(),
                    (std::max)(3.0f, camera->GetNearClip() * 3.0f)) *
                    safeOverlayScale;
                const float tangent = std::tan(camera->GetFovYRad() * 0.5f);
                const float nearHalfHeight = tangent * nearDistance;
                const float nearHalfWidth = nearHalfHeight * aspect;
                const float farHalfHeight = tangent * farDistance;
                const float farHalfWidth = farHalfHeight * aspect;
                const MATH::Mat4 world = object->Transform().GetWorldMatrix();
                const CameraOverlayBasis basis = BuildCameraOverlayBasis(world);

                CameraOverlayEntry entry{};
                entry.objectId = object->GetDocumentId();
                entry.name = object->GetName();
                entry.enabled = camera->IsEnabled();
                entry.selected = entry.objectId == selectedObjectId;
                entry.points = {
                    basis.position,
                    TransformCameraOverlayPoint(basis, -nearHalfWidth, -nearHalfHeight, nearDistance),
                    TransformCameraOverlayPoint(basis,  nearHalfWidth, -nearHalfHeight, nearDistance),
                    TransformCameraOverlayPoint(basis,  nearHalfWidth,  nearHalfHeight, nearDistance),
                    TransformCameraOverlayPoint(basis, -nearHalfWidth,  nearHalfHeight, nearDistance),
                    TransformCameraOverlayPoint(basis, -farHalfWidth, -farHalfHeight, farDistance),
                    TransformCameraOverlayPoint(basis,  farHalfWidth, -farHalfHeight, farDistance),
                    TransformCameraOverlayPoint(basis,  farHalfWidth,  farHalfHeight, farDistance),
                    TransformCameraOverlayPoint(basis, -farHalfWidth,  farHalfHeight, farDistance),
                };
                result.push_back(std::move(entry));
            }
            return result;
        }

        SceneObjectId DrawCameraOverlays(
            const DocumentSceneBase& scene,
            SceneObjectId selectedObjectId,
            const Camera3D& viewCamera,
            const ImVec2& origin,
            const ImVec2& size,
            bool acceptSelection,
            const EditorViewVisualizationState& visualization) {

            if (!visualization.showCameraOverlays) {
                return {};
            }

            constexpr std::array<std::array<int, 2>, 16> kEdges{ {
                { 1, 2 }, { 2, 3 }, { 3, 4 }, { 4, 1 },
                { 5, 6 }, { 6, 7 }, { 7, 8 }, { 8, 5 },
                { 1, 5 }, { 2, 6 }, { 3, 7 }, { 4, 8 },
                { 0, 5 }, { 0, 6 }, { 0, 7 }, { 0, 8 },
            } };

            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(
                origin,
                { origin.x + size.x, origin.y + size.y },
                true);
            const ImVec2 mouse = ImGui::GetMousePos();
            SceneObjectId nearestObject{};
            float nearestDistanceSquared =
                kCameraHitDistance * kCameraHitDistance;

            const std::vector<CameraOverlayEntry> cameras =
                GatherCameraOverlays(
                    scene,
                    selectedObjectId,
                    viewCamera.GetAspect(),
                    visualization.cameraOverlayScale);
            for (const CameraOverlayEntry& camera : cameras) {
                if (visualization.showOnlySelectedCamera && !camera.selected) {
                    continue;
                }
                const ImU32 color = !camera.enabled
                    ? IM_COL32(150, 158, 170, 220)
                    : (camera.selected
                        ? IM_COL32(255, 204, 104, 255)
                        : IM_COL32(108, 224, 255, 245));
                std::array<ImVec2, 9> screenPoints{};
                std::array<bool, 9> projected{};
                for (size_t index = 0; index < camera.points.size(); ++index) {
                    projected[index] = ProjectWorldToViewport(
                        viewCamera,
                        camera.points[index],
                        origin,
                        size,
                        screenPoints[index]);
                }

                for (const auto& edge : kEdges) {
                    if (!projected[edge[0]] || !projected[edge[1]]) {
                        continue;
                    }
                    drawList->AddLine(
                        screenPoints[edge[0]],
                        screenPoints[edge[1]],
                        color,
                        camera.selected ? 1.8f : 1.1f);
                    if (acceptSelection) {
                        const float distanceSquared = DistanceSquaredToSegment(
                            mouse,
                            screenPoints[edge[0]],
                            screenPoints[edge[1]]);
                        if (distanceSquared < nearestDistanceSquared) {
                            nearestDistanceSquared = distanceSquared;
                            nearestObject = camera.objectId;
                        }
                    }
                }

                if (projected[0]) {
                    drawList->AddCircleFilled(screenPoints[0], 4.5f, color, 10);
                    const std::string label = camera.name.empty()
                        ? "Camera " + std::to_string(camera.objectId.value)
                        : camera.name;
                    const ImVec2 textPosition{
                        screenPoints[0].x + 8.0f,
                        screenPoints[0].y - 8.0f
                    };
                    drawList->AddText(textPosition, color, label.c_str());
                    if (acceptSelection) {
                        const ImVec2 textSize = ImGui::CalcTextSize(label.c_str());
                        const bool labelHovered =
                            mouse.x >= textPosition.x - 3.0f &&
                            mouse.y >= textPosition.y - 3.0f &&
                            mouse.x <= textPosition.x + textSize.x + 3.0f &&
                            mouse.y <= textPosition.y + textSize.y + 3.0f;
                        const float dx = mouse.x - screenPoints[0].x;
                        const float dy = mouse.y - screenPoints[0].y;
                        const float pointDistanceSquared = dx * dx + dy * dy;
                        if (labelHovered ||
                            pointDistanceSquared < nearestDistanceSquared) {
                            nearestDistanceSquared = labelHovered
                                ? 0.0f
                                : pointDistanceSquared;
                            nearestObject = camera.objectId;
                        }
                    }
                }
            }
            drawList->PopClipRect();
            return nearestObject;
        }

        const char* ModeLabel(DirectorViewMode mode) {
            switch (mode) {
            case DirectorViewMode::LookThrough:
                return "Look Through";
            case DirectorViewMode::Pilot:
                return "Pilot";
            case DirectorViewMode::Free:
            default:
                return "Free";
            }
        }

        const char* ShadingModeLabel(EditorViewShadingMode mode) {
            switch (mode) {
            case EditorViewShadingMode::Neutral:
                return "Neutral";
            case EditorViewShadingMode::Unlit:
                return "Unlit";
            case EditorViewShadingMode::Lit:
            default:
                return "Lit";
            }
        }

        RENDER3D::EDITORVIEW::EditorInteractiveShadingMode ResolveShadingMode(
            EditorViewShadingMode mode) {

            switch (mode) {
            case EditorViewShadingMode::Neutral:
                return RENDER3D::EDITORVIEW::EditorInteractiveShadingMode::Neutral;
            case EditorViewShadingMode::Unlit:
                return RENDER3D::EDITORVIEW::EditorInteractiveShadingMode::Unlit;
            case EditorViewShadingMode::Lit:
            default:
                return RENDER3D::EDITORVIEW::EditorInteractiveShadingMode::Lit;
            }
        }

        const char* CameraOverlayModeLabel(
            const EditorViewVisualizationState& visualization) {

            if (!visualization.showCameraOverlays) {
                return "Off";
            }
            return visualization.showOnlySelectedCamera
                ? "Selected"
                : "All";
        }

        bool DrawModeButton(const char* label, bool selected) {
            if (selected) {
                ImGui::PushStyleColor(
                    ImGuiCol_Button,
                    ImVec4(0.18f, 0.38f, 0.58f, 1.0f));
                ImGui::PushStyleColor(
                    ImGuiCol_ButtonHovered,
                    ImVec4(0.22f, 0.46f, 0.68f, 1.0f));
                ImGui::PushStyleColor(
                    ImGuiCol_ButtonActive,
                    ImVec4(0.16f, 0.34f, 0.52f, 1.0f));
            }
            const bool clicked = ImGui::Button(label);
            if (selected) {
                ImGui::PopStyleColor(3);
            }
            return clicked;
        }

        DirectorCameraPose CameraPoseFromView(const Camera3D& camera) {
            MATH::Vec3 forward = camera.GetTarget() - camera.GetPosition();
            if (MATH::Length(forward) <= 1.0e-5f) {
                forward = { 0.0f, 0.0f, 1.0f };
            } else {
                forward = MATH::Normalize(forward);
            }
            const float yaw = std::atan2(forward.x, forward.z);
            const float pitch = std::asin(std::clamp(forward.y, -1.0f, 1.0f));

            DirectorCameraPose pose{};
            pose.position = camera.GetPosition();
            pose.rotation = MATH::Quat::FromEulerXYZ(-pitch, yaw, 0.0f);
            return pose;
        }

        uint32_t ExtentDimension(float value) {
            if (!std::isfinite(value) || value <= 0.0f) {
                return 0;
            }
            return static_cast<uint32_t>((std::min)(
                value,
                static_cast<float>((std::numeric_limits<uint32_t>::max)())));
        }
    }
#endif

    DirectorViewPanel::DirectorViewPanel() {
        currentViewCamera_ = freeCamera_.GetCamera();
    }

    DirectorViewPanelResult DirectorViewPanel::DrawContents(
        const DocumentSceneBase& scene,
        EditorViewInstance& view,
        SceneObjectId selectedObjectId,
        const EditorTransformGizmoState& gizmoState,
        float deltaTime,
        bool runtimePlayActive) {

        DirectorViewPanelResult result{};
#if defined(HIKARI_WITH_EDITOR)
        bool toolbarCameraChanged = false;
        if (mode_ != DirectorViewMode::Free &&
            (!IsEnabledCamera(scene, targetCameraObjectId_) ||
                (mode_ == DirectorViewMode::Pilot &&
                    HasDocumentParent(scene, targetCameraObjectId_)))) {
            SetMode(DirectorViewMode::Free);
        }
        if (runtimePlayActive && mode_ == DirectorViewMode::Pilot) {
            SetMode(DirectorViewMode::Free);
        }

        SceneObjectId toolbarTarget = IsEnabledCamera(scene, selectedObjectId)
            ? selectedObjectId
            : targetCameraObjectId_;
        const GameObject* selectedObject =
            FindRuntimeObject(scene, selectedObjectId);
        const bool selectedObjectIsCamera = selectedObject != nullptr &&
            selectedObject->GetComponent<CameraComponent>() != nullptr;
        const bool targetEnabled = IsEnabledCamera(scene, toolbarTarget);
        const bool targetHasParent = targetEnabled &&
            HasDocumentParent(scene, toolbarTarget);

        const float toolbarHeight =
            ImGui::GetFrameHeight() * 3.0f +
            ImGui::GetStyle().ItemSpacing.y * 2.0f + 10.0f;
        ImGui::PushStyleColor(
            ImGuiCol_ChildBg,
            ImVec4(0.045f, 0.052f, 0.064f, 1.0f));
        ImGui::BeginChild(
            "##DirectorToolbar",
            ImVec2(0.0f, toolbarHeight),
            false,
            ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse);

        if (DrawModeButton("Free", mode_ == DirectorViewMode::Free)) {
            SetMode(DirectorViewMode::Free);
        }
        ImGui::SameLine();
        if (!targetEnabled) {
            ImGui::BeginDisabled();
        }
        if (DrawModeButton(
                "Look Through",
                mode_ == DirectorViewMode::LookThrough)) {
            SetTargetCamera(toolbarTarget);
            SetMode(DirectorViewMode::LookThrough);
        }
        if (!targetEnabled) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        const bool pilotDisabled = !targetEnabled || targetHasParent ||
            runtimePlayActive;
        if (pilotDisabled) {
            ImGui::BeginDisabled();
        }
        if (DrawModeButton("Pilot", mode_ == DirectorViewMode::Pilot)) {
            Camera3D targetCamera{};
            if (scene.TryResolveCameraObjectView(
                    toolbarTarget,
                    (std::max)(view.extent.Aspect(), 0.05f),
                    targetCamera)) {
                SetTargetCamera(toolbarTarget);
                pilotCamera_.ResetFromCamera(targetCamera);
                SetMode(DirectorViewMode::Pilot);
            }
        }
        if (pilotDisabled) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        const bool alignDisabled = !targetEnabled || targetHasParent ||
            runtimePlayActive;
        if (alignDisabled) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Align Camera to View")) {
            result.alignCameraRequested = true;
            result.alignCameraObjectId = toolbarTarget;
            result.alignPose = CameraPoseFromView(currentViewCamera_);
        }
        if (alignDisabled) {
            ImGui::EndDisabled();
        }
        if (targetHasParent &&
            ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(
                "Pilot and Align are disabled for parented cameras in this stage.");
        }

        ImGui::SameLine();
        if (mode_ == DirectorViewMode::Pilot) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.58f, 0.20f, 1.0f),
                "PILOTING %llu",
                static_cast<unsigned long long>(targetCameraObjectId_.value));
        } else {
            ImGui::TextDisabled("%s", ModeLabel(mode_));
        }

        ImGui::Spacing();
        const bool frameDisabled = selectedObject == nullptr || runtimePlayActive;
        if (frameDisabled) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Button("Frame Selected")) {
            SetMode(DirectorViewMode::Free);
            freeCamera_.Focus(ExtractPosition(
                selectedObject->Transform().GetWorldMatrix()));
            toolbarCameraChanged = true;
        }
        if (frameDisabled) {
            ImGui::EndDisabled();
        }

        if (runtimePlayActive) {
            ImGui::BeginDisabled();
        }
        const auto applyPreset = [this, &toolbarCameraChanged](
            EditorDirectorCameraViewPreset preset) {
            SetMode(DirectorViewMode::Free);
            freeCamera_.ApplyViewPreset(preset);
            toolbarCameraChanged = true;
        };
        ImGui::SameLine();
        if (ImGui::Button("Perspective")) {
            applyPreset(EditorDirectorCameraViewPreset::Perspective);
        }
        ImGui::SameLine();
        if (ImGui::Button("Top")) {
            applyPreset(EditorDirectorCameraViewPreset::Top);
        }
        ImGui::SameLine();
        if (ImGui::Button("Front")) {
            applyPreset(EditorDirectorCameraViewPreset::Front);
        }
        ImGui::SameLine();
        if (ImGui::Button("Right")) {
            applyPreset(EditorDirectorCameraViewPreset::Right);
        }
        if (runtimePlayActive) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        ImGui::TextUnformatted("Shading");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(92.0f);
        if (ImGui::BeginCombo(
                "##DirectorShadingMode",
                ShadingModeLabel(view.visualization.shadingMode))) {
            const EditorViewShadingMode modes[] = {
                EditorViewShadingMode::Lit,
                EditorViewShadingMode::Neutral,
                EditorViewShadingMode::Unlit,
            };
            for (EditorViewShadingMode candidate : modes) {
                const bool selected = candidate == view.visualization.shadingMode;
                if (ImGui::Selectable(ShadingModeLabel(candidate), selected)) {
                    view.visualization.shadingMode = candidate;
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }

        ImGui::SameLine();
        ImGui::TextUnformatted("Exposure");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(86.0f);
        ImGui::SliderFloat(
            "##DirectorDisplayExposure",
            &view.visualization.displayExposure,
            0.25f,
            4.0f,
            "%.2fx");

        ImGui::SameLine();
        ImGui::TextUnformatted("Cameras");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(82.0f);
        if (ImGui::BeginCombo(
                "##DirectorCameraOverlays",
                CameraOverlayModeLabel(view.visualization))) {
            if (ImGui::Selectable(
                    "All",
                    view.visualization.showCameraOverlays &&
                        !view.visualization.showOnlySelectedCamera)) {
                view.visualization.showCameraOverlays = true;
                view.visualization.showOnlySelectedCamera = false;
            }
            if (ImGui::Selectable(
                    "Selected",
                    view.visualization.showCameraOverlays &&
                        view.visualization.showOnlySelectedCamera)) {
                view.visualization.showCameraOverlays = true;
                view.visualization.showOnlySelectedCamera = true;
            }
            if (ImGui::Selectable(
                    "Off",
                    !view.visualization.showCameraOverlays)) {
                view.visualization.showCameraOverlays = false;
                view.visualization.showOnlySelectedCamera = false;
            }
            ImGui::EndCombo();
        }

        if (view.visualization.showCameraOverlays) {
            ImGui::SameLine();
            ImGui::TextUnformatted("Size");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(78.0f);
            ImGui::SliderFloat(
                "##DirectorCameraOverlayScale",
                &view.visualization.cameraOverlayScale,
                0.5f,
                2.0f,
                "%.1fx");
        }

        ImGui::Spacing();
        if (selectedObjectIsCamera &&
            gizmoState.operation == EditorTransformGizmoOperation::Scale) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.66f, 0.30f, 1.0f),
                "Camera scale is not a lens control; edit FOV in Camera settings.");
        } else if (mode_ == DirectorViewMode::Pilot) {
            ImGui::TextColored(
                ImVec4(1.0f, 0.66f, 0.30f, 1.0f),
                "Hold RMB: look + WASDQE move | Shift: boost | Esc: exit Pilot");
        } else {
            ImGui::TextDisabled(
                "Hold RMB: look + WASDQE move | Shift: boost | Alt+LMB: orbit | MMB: pan | F: focus");
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();

        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        canvasSize.x = (std::max)(canvasSize.x, kMinimumCanvasSize);
        canvasSize.y = (std::max)(canvasSize.y, kMinimumCanvasSize);
        const ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
        const EditorViewportFit viewportFit = FitEditorViewport(
            canvasSize.x,
            canvasSize.y,
            kDirectorViewportAspect);
        const ImVec2 imageSize{ viewportFit.width, viewportFit.height };
        const ImVec2 imageOrigin{
            canvasOrigin.x + viewportFit.offsetX,
            canvasOrigin.y + viewportFit.offsetY
        };
        ImGui::GetWindowDrawList()->AddRectFilled(
            canvasOrigin,
            { canvasOrigin.x + canvasSize.x, canvasOrigin.y + canvasSize.y },
            IM_COL32(12, 16, 22, 255));
        ImGui::SetCursorScreenPos(imageOrigin);
        view.extent.width = ExtentDimension(imageSize.x);
        view.extent.height = ExtentDimension(imageSize.y);
        const float aspect = (std::max)(view.extent.Aspect(), 0.05f);

        const RENDER3D::EDITORVIEW::EditorInteractiveViewOutput output =
            RENDER3D::EDITORVIEW::GetOutput(view.renderViewId);
        const bool outputReady = output.ready && output.colorSrv.ptr != 0 &&
            output.width > 0 && output.height > 0 &&
            output.sceneRevision == scene.GetSceneDocumentRevision();
        if (outputReady) {
            const ImTextureID texture = reinterpret_cast<ImTextureID>(
                static_cast<uintptr_t>(output.colorSrv.ptr));
            ImGui::Image(texture, imageSize);
        } else {
            ImGui::InvisibleButton(
                "##DirectorViewportCanvas",
                imageSize,
                ImGuiButtonFlags_MouseButtonLeft |
                    ImGuiButtonFlags_MouseButtonRight |
                    ImGuiButtonFlags_MouseButtonMiddle);
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->AddRectFilled(
                imageOrigin,
                { imageOrigin.x + imageSize.x, imageOrigin.y + imageSize.y },
                IM_COL32(9, 12, 16, 255));
            drawList->AddText(
                { imageOrigin.x + 14.0f, imageOrigin.y + 14.0f },
                IM_COL32(184, 198, 211, 255),
                "Waiting for Director View output");
        }

        const bool imageHovered = ImGui::IsItemHovered();
        const bool windowFocused =
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        const ImGuiIO& io = ImGui::GetIO();
        EditorViewInputSubmission inputSubmission{};
        inputSubmission.rect = {
            imageOrigin.x,
            imageOrigin.y,
            imageSize.x,
            imageSize.y
        };
        inputSubmission.visible = ImGui::IsItemVisible();
        inputSubmission.focused = windowFocused;
        inputSubmission.hovered = imageHovered;
        inputSubmission.rightMouseDown =
            ImGui::IsMouseDown(ImGuiMouseButton_Right);
        inputSubmission.rightMouseClicked =
            ImGui::IsMouseClicked(ImGuiMouseButton_Right);
        inputSubmission.middleMouseDown =
            ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        inputSubmission.middleMouseClicked =
            ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
        inputSubmission.leftMouseDown =
            ImGui::IsMouseDown(ImGuiMouseButton_Left);
        inputSubmission.leftMouseClicked =
            ImGui::IsMouseClicked(ImGuiMouseButton_Left);
        inputSubmission.altDown = io.KeyAlt;
        // Gizmo capture belongs to the current frame. Clear the previous-frame
        // value before arbitrating a new RMB/MMB/Alt+LMB press.
        inputRouter_.SetGizmoCapture(view.renderViewId, false);
        const EditorViewInputState& inputState = inputRouter_.Submit(
            view.renderViewId,
            inputSubmission);

        const bool navigationPressed = imageHovered &&
            (inputSubmission.rightMouseClicked ||
                inputSubmission.middleMouseClicked ||
                (inputSubmission.leftMouseClicked && inputSubmission.altDown));
        if (navigationPressed) {
            ImGui::SetWindowFocus();
            ImGui::ClearActiveID();
        }

        view.interaction.visible = inputState.visible;
        view.interaction.focused = inputState.focused;
        view.interaction.hovered = inputState.hovered;
        view.interaction.mouseCaptured = inputState.IsMouseCaptured();
        view.interaction.keyboardActive = inputState.AcceptsKeyboard();

        if (mode_ == DirectorViewMode::Pilot &&
            ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            SetMode(DirectorViewMode::Free);
        }

        EditorDirectorCameraInput cameraInput{};
        const bool allowNavigation = mode_ != DirectorViewMode::LookThrough &&
            !runtimePlayActive && !inputState.gizmoCaptured;
        if (allowNavigation) {
            cameraInput.lookActive = inputState.rightMouseCaptured;
            cameraInput.orbitActive = inputState.orbitMouseCaptured;
            cameraInput.panActive = inputState.middleMouseCaptured;
            cameraInput.mouseDeltaX = io.MouseDelta.x;
            cameraInput.mouseDeltaY = io.MouseDelta.y;
            cameraInput.wheelDelta = inputState.AcceptsWheel()
                ? io.MouseWheel
                : 0.0f;
            cameraInput.fast = io.KeyShift;
            if (inputState.rightMouseCaptured) {
                cameraInput.moveForward = ImGui::IsKeyDown(ImGuiKey_W);
                cameraInput.moveBackward = ImGui::IsKeyDown(ImGuiKey_S);
                cameraInput.moveRight = ImGui::IsKeyDown(ImGuiKey_D);
                cameraInput.moveLeft = ImGui::IsKeyDown(ImGuiKey_A);
                cameraInput.moveUp = ImGui::IsKeyDown(ImGuiKey_E);
                cameraInput.moveDown = ImGui::IsKeyDown(ImGuiKey_Q);
            }
        }

        bool cameraChanged = toolbarCameraChanged;
        if (allowNavigation && inputState.AcceptsKeyboard() &&
            imageHovered && !io.WantTextInput &&
            ImGui::IsKeyPressed(ImGuiKey_F, false)) {
            if (const GameObject* selectedObject =
                    FindRuntimeObject(scene, selectedObjectId)) {
                ActiveController().Focus(
                    ExtractPosition(
                        selectedObject->Transform().GetWorldMatrix()));
                cameraChanged = true;
            }
        }

        if (allowNavigation) {
            cameraChanged = ActiveController().Update(
                cameraInput,
                deltaTime,
                aspect) || cameraChanged;
        }

        if (mode_ == DirectorViewMode::LookThrough) {
            if (!scene.TryResolveCameraObjectView(
                    targetCameraObjectId_,
                    aspect,
                    resolvedLookCamera_)) {
                SetMode(DirectorViewMode::Free);
                currentViewCamera_ = freeCamera_.GetCamera();
            } else {
                currentViewCamera_ = resolvedLookCamera_;
            }
        } else {
            currentViewCamera_ = ActiveController().GetCamera();
        }

        if (mode_ == DirectorViewMode::Pilot && cameraChanged) {
            result.pilotPoseChanged = true;
            result.pilotCameraObjectId = targetCameraObjectId_;
            result.pilotPose = pilotCamera_.GetPose();
        }

        if (mode_ == DirectorViewMode::Free) {
            view.cameraBinding.kind = EditorViewCameraSourceKind::OwnedEditorCamera;
            view.cameraBinding.sceneObjectId = {};
        } else {
            view.cameraBinding.kind = EditorViewCameraSourceKind::SceneCameraObject;
            view.cameraBinding.sceneObjectId = targetCameraObjectId_;
        }

        const bool projectionChanged =
            std::abs(lastAspect_ - aspect) > 1.0e-5f;
        if (cameraChanged || projectionChanged || cameraCutPending_) {
            ++cameraRevision_;
        }
        lastAspect_ = aspect;

        const bool gizmoAllowed = !runtimePlayActive &&
            mode_ != DirectorViewMode::Pilot;
        if (gizmoAllowed &&
            !(selectedObjectIsCamera &&
                gizmoState.operation == EditorTransformGizmoOperation::Scale)) {
            if (selectedObject != nullptr) {
                GameObject proxy{ "Director Gizmo Proxy" };
                proxy.SetDocumentId(selectedObjectId);
                proxy.Transform() = selectedObject->Transform();
                if (selectedObjectIsCamera) {
                    proxy.Transform().scale = { 1.0f, 1.0f, 1.0f };
                    proxy.Transform().useExplicitMatrix = false;
                }
                EditorTransformGizmoState directorGizmoState = gizmoState;
                if (io.KeyCtrl) {
                    directorGizmoState.snapEnabled = true;
                }
                const EditorTransformGizmoResult gizmoResult =
                    transformGizmo_.Draw(
                        proxy,
                        currentViewCamera_,
                        directorGizmoState,
                        {
                            imageOrigin.x,
                            imageOrigin.y,
                            imageSize.x,
                            imageSize.y
                        });
                result.gizmo.interacting = gizmoResult.interacting;
                result.gizmo.changed = gizmoResult.changed;
                result.gizmo.objectId = selectedObjectId;
                result.gizmo.transform = gizmoResult.transform;
                result.gizmo.rotation = gizmoResult.rotation;
            }
        }
        inputRouter_.SetGizmoCapture(
            view.renderViewId,
            result.gizmo.interacting);
        view.interaction.gizmoCaptured = result.gizmo.interacting;

        const bool cameraSelectionClicked = imageHovered &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
            !io.KeyAlt && !result.gizmo.interacting;
        const SceneObjectId clickedCamera = DrawCameraOverlays(
            scene,
            selectedObjectId,
            currentViewCamera_,
            imageOrigin,
            imageSize,
            cameraSelectionClicked,
            view.visualization);
        if (cameraSelectionClicked && clickedCamera.value != 0) {
            result.selectionRequested = true;
            result.selectedObjectId = clickedCamera;
            SetTargetCamera(clickedCamera);
        }

        RENDER3D::EDITORVIEW::EditorInteractiveViewRequest request{};
        request.viewId = view.renderViewId;
        request.cameraFrame.camera = currentViewCamera_;
        request.cameraFrame.sourceCameraObjectId =
            mode_ == DirectorViewMode::Free
                ? 0
                : targetCameraObjectId_.value;
        request.cameraFrame.revision = cameraRevision_;
        request.cameraFrame.cameraCut = cameraCutPending_;
        request.cameraFrame.projectionChanged = projectionChanged;
        request.cameraFrame.valid = true;
        request.width = view.extent.width;
        request.height = view.extent.height;
        request.sceneRevision = scene.GetSceneDocumentRevision();
        request.visible = view.interaction.visible;
        request.shadingMode = ResolveShadingMode(
            view.visualization.shadingMode);
        request.displayExposure = view.visualization.displayExposure;
        // Camera overlays and the transform gizmo are drawn explicitly by this
        // panel. Replaying every Scene debug helper here creates duplicate light
        // markers and oversized frusta that obscure camera editing.
        request.drawDebug = false;
        RENDER3D::EDITORVIEW::SubmitRequest(request);
        cameraCutPending_ = false;

        ImGui::SetCursorScreenPos({
            canvasOrigin.x,
            canvasOrigin.y + canvasSize.y
        });
        ImGui::Dummy(ImVec2(1.0f, 1.0f));

        result.mode = mode_;
        result.targetCameraObjectId = targetCameraObjectId_;
#else
        (void)scene;
        (void)view;
        (void)selectedObjectId;
        (void)gizmoState;
        (void)deltaTime;
        (void)runtimePlayActive;
#endif
        return result;
    }

    void DirectorViewPanel::SetTargetCamera(
        SceneObjectId cameraObjectId) noexcept {
        if (cameraObjectId == targetCameraObjectId_) {
            return;
        }
        if (mode_ == DirectorViewMode::Pilot) {
            SetMode(DirectorViewMode::Free);
        }
        targetCameraObjectId_ = cameraObjectId;
    }

    void DirectorViewPanel::ExitPilot() noexcept {
        if (mode_ == DirectorViewMode::Pilot) {
            SetMode(DirectorViewMode::Free);
        }
    }

    void DirectorViewPanel::ResetForScene() {
#if defined(HIKARI_WITH_EDITOR)
        RENDER3D::EDITORVIEW::ClearRequest(kEditorDirectorRenderViewId);
#endif
        mode_ = DirectorViewMode::Free;
        targetCameraObjectId_ = {};
        freeCamera_.Reset();
        pilotCamera_.Reset();
        currentViewCamera_ = freeCamera_.GetCamera();
        inputRouter_.Clear(kEditorDirectorRenderViewId);
        cameraRevision_ = 1;
        lastAspect_ = 0.0f;
        cameraCutPending_ = true;
    }

    void DirectorViewPanel::LeaveWorkspace() {
#if defined(HIKARI_WITH_EDITOR)
        RENDER3D::EDITORVIEW::ClearRequest(kEditorDirectorRenderViewId);
#endif
        SetMode(DirectorViewMode::Free);
        targetCameraObjectId_ = {};
        inputRouter_.Clear(kEditorDirectorRenderViewId);
    }

    DirectorViewMode DirectorViewPanel::GetMode() const noexcept {
        return mode_;
    }

    SceneObjectId DirectorViewPanel::GetTargetCameraObjectId() const noexcept {
        return targetCameraObjectId_;
    }

    const Camera3D& DirectorViewPanel::GetViewCamera() const noexcept {
        return currentViewCamera_;
    }

    EditorDirectorCameraController& DirectorViewPanel::ActiveController() noexcept {
        return mode_ == DirectorViewMode::Pilot ? pilotCamera_ : freeCamera_;
    }

    const EditorDirectorCameraController& DirectorViewPanel::ActiveController() const noexcept {
        return mode_ == DirectorViewMode::Pilot ? pilotCamera_ : freeCamera_;
    }

    void DirectorViewPanel::SetMode(DirectorViewMode mode) noexcept {
        if (mode_ == mode) {
            return;
        }
        mode_ = mode;
        cameraCutPending_ = true;
    }

} // namespace HIKARI::EDITOR
