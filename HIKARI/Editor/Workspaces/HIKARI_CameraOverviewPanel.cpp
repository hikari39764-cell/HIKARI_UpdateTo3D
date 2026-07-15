#include "HIKARI_CameraOverviewPanel.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "Editor/HIKARI_EditorContext.h"
#include "Scene/Components/HIKARI_CameraComponent.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "Scene/Scenes/HIKARI_DocumentSceneBase.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

#if defined(HIKARI_WITH_EDITOR)
    namespace {

        constexpr float kMinOverviewUnits = 2.0f;
        constexpr float kMaxOverviewUnits = 10000.0f;
        constexpr float kMinCanvasSize = 1.0f;
        constexpr float kCameraHitRadius = 12.0f;
        constexpr float kVectorEpsilon = 0.00001f;
        constexpr float kRadiansToDegrees = 57.29577951308232f;

        struct OverviewCameraEntry {
            SceneObjectId objectId{};
            std::string name{};
            MATH::Vec2 positionXZ{};
            MATH::Vec2 forwardXZ{ 0.0f, 1.0f };
            float fovYRad = 1.0471975512f;
            float nearClip = 0.1f;
            float farClip = 100.0f;
            bool enabled = true;
            bool isDefault = false;
            bool isSelected = false;
            bool isPreviewed = false;
            bool hasParent = false;
        };

        MATH::Vec3 ExtractAxis(const MATH::Mat4& matrix, int column) {
            return {
                matrix.m[column][0],
                matrix.m[column][1],
                matrix.m[column][2]
            };
        }

        const SceneObjectData* FindDocumentObject(
            const SceneDocument& document,
            SceneObjectId objectId) {

            const auto it = std::find_if(
                document.objects.begin(),
                document.objects.end(),
                [objectId](const SceneObjectData& object) {
                    return object.id == objectId;
                });
            return it == document.objects.end() ? nullptr : &*it;
        }

        GameObject* FindRuntimeObject(DocumentSceneBase& scene, SceneObjectId objectId) {
            for (const auto& object : scene.GetWorld().GetObjects()) {
                if (object && object->GetDocumentId() == objectId) {
                    return object.get();
                }
            }
            return nullptr;
        }

        void SelectRuntimeObject(EditorContext& context, GameObject* object) {
            context.selection.selectedObject = object;
            context.selection.selectedAsset = nullptr;
            context.selection.selectedAssetGuid.clear();
            context.selection.selectedAssetPath.clear();
        }

        std::vector<OverviewCameraEntry> GatherCameras(
            DocumentSceneBase& scene,
            const EditorContext& context) {

            std::vector<OverviewCameraEntry> cameras{};
            const SceneDocument& document = scene.GetSceneDocument();
            const SceneObjectId selectedId = context.selection.selectedObject
                ? context.selection.selectedObject->GetDocumentId()
                : SceneObjectId{};
            const SceneObjectId previewId = scene.IsEditorCameraPreviewActive()
                ? scene.GetEditorCameraPreviewObjectId()
                : SceneObjectId{};

            for (const auto& object : scene.GetWorld().GetObjects()) {
                if (!object) {
                    continue;
                }
                const CameraComponent* camera = object->GetComponent<CameraComponent>();
                if (!camera) {
                    continue;
                }

                const MATH::Mat4 worldMatrix = object->Transform().GetWorldMatrix();
                const MATH::Vec3 position = ExtractAxis(worldMatrix, 3);
                const MATH::Vec3 forward = ExtractAxis(worldMatrix, 2);
                MATH::Vec2 forwardXZ{ forward.x, forward.z };
                const float forwardLength = std::sqrt(
                    forwardXZ.x * forwardXZ.x + forwardXZ.y * forwardXZ.y);
                if (forwardLength > kVectorEpsilon) {
                    forwardXZ.x /= forwardLength;
                    forwardXZ.y /= forwardLength;
                } else {
                    forwardXZ = { 0.0f, 1.0f };
                }

                OverviewCameraEntry entry{};
                entry.objectId = object->GetDocumentId();
                entry.name = object->GetName();
                entry.positionXZ = { position.x, position.z };
                entry.forwardXZ = forwardXZ;
                entry.fovYRad = camera->GetFovYRad();
                entry.nearClip = camera->GetNearClip();
                entry.farClip = camera->GetFarClip();
                entry.enabled = camera->IsEnabled();
                entry.isDefault = document.camera.defaultCameraObjectId == entry.objectId;
                entry.isSelected = selectedId == entry.objectId;
                entry.isPreviewed = previewId == entry.objectId;
                if (const SceneObjectData* documentObject =
                        FindDocumentObject(document, entry.objectId)) {
                    entry.hasParent = documentObject->parent.has_value();
                }
                cameras.push_back(std::move(entry));
            }

            std::sort(
                cameras.begin(),
                cameras.end(),
                [](const OverviewCameraEntry& lhs, const OverviewCameraEntry& rhs) {
                    if (lhs.name != rhs.name) {
                        return lhs.name < rhs.name;
                    }
                    return lhs.objectId.value < rhs.objectId.value;
                });
            return cameras;
        }

        uint32_t ToExtentDimension(float value) {
            if (!std::isfinite(value) || value <= 0.0f) {
                return 0;
            }
            const float limit = static_cast<float>((std::numeric_limits<uint32_t>::max)());
            return static_cast<uint32_t>((std::min)(value, limit));
        }

        float NiceGridStep(float targetStep) {
            targetStep = (std::max)(targetStep, 0.0001f);
            const float magnitude = std::pow(10.0f, std::floor(std::log10(targetStep)));
            const float normalized = targetStep / magnitude;
            if (normalized <= 1.0f) {
                return magnitude;
            }
            if (normalized <= 2.0f) {
                return 2.0f * magnitude;
            }
            if (normalized <= 5.0f) {
                return 5.0f * magnitude;
            }
            return 10.0f * magnitude;
        }

        ImVec2 WorldToScreen(
            const MATH::Vec2& world,
            const MATH::Vec2& center,
            const ImVec2& canvasCenter,
            float pixelsPerUnit) {

            return {
                canvasCenter.x + (world.x - center.x) * pixelsPerUnit,
                canvasCenter.y - (world.y - center.y) * pixelsPerUnit
            };
        }

        MATH::Vec2 ScreenToWorld(
            const ImVec2& screen,
            const MATH::Vec2& center,
            const ImVec2& canvasCenter,
            float pixelsPerUnit) {

            return {
                center.x + (screen.x - canvasCenter.x) / pixelsPerUnit,
                center.y - (screen.y - canvasCenter.y) / pixelsPerUnit
            };
        }

        void FitCameras(
            EditorViewInstance& view,
            const std::vector<OverviewCameraEntry>& cameras) {

            if (cameras.empty()) {
                view.overviewCenterXZ = {};
                view.overviewUnitsPerScreen = 40.0f;
                return;
            }

            MATH::Vec2 minimum = cameras.front().positionXZ;
            MATH::Vec2 maximum = cameras.front().positionXZ;
            for (const OverviewCameraEntry& camera : cameras) {
                minimum.x = (std::min)(minimum.x, camera.positionXZ.x);
                minimum.y = (std::min)(minimum.y, camera.positionXZ.y);
                maximum.x = (std::max)(maximum.x, camera.positionXZ.x);
                maximum.y = (std::max)(maximum.y, camera.positionXZ.y);
            }

            view.overviewCenterXZ = {
                (minimum.x + maximum.x) * 0.5f,
                (minimum.y + maximum.y) * 0.5f
            };
            const float spanX = maximum.x - minimum.x;
            const float spanZ = maximum.y - minimum.y;
            const float aspect = view.extent.IsValid()
                ? (std::max)(view.extent.Aspect(), 0.1f)
                : 16.0f / 9.0f;
            const float fittedWidth = (std::max)(spanX, spanZ * aspect);
            view.overviewUnitsPerScreen = std::clamp(
                (std::max)(fittedWidth * 1.35f, 12.0f),
                kMinOverviewUnits,
                kMaxOverviewUnits);
        }

        ImU32 ResolveCameraColor(const OverviewCameraEntry& camera) {
            if (!camera.enabled) {
                return IM_COL32(110, 116, 126, 230);
            }
            if (camera.isSelected) {
                return IM_COL32(255, 194, 92, 255);
            }
            if (camera.isDefault) {
                return IM_COL32(90, 222, 148, 255);
            }
            return IM_COL32(94, 189, 246, 250);
        }

        void DrawGrid(
            ImDrawList& drawList,
            const ImVec2& canvasMin,
            const ImVec2& canvasMax,
            const ImVec2& canvasCenter,
            const EditorViewInstance& view,
            float pixelsPerUnit) {

            const float gridStep = NiceGridStep(view.overviewUnitsPerScreen / 12.0f);
            const float halfWidthWorld = view.overviewUnitsPerScreen * 0.5f;
            const float halfHeightWorld = halfWidthWorld / (std::max)(view.extent.Aspect(), 0.1f);
            const float minX = view.overviewCenterXZ.x - halfWidthWorld;
            const float maxX = view.overviewCenterXZ.x + halfWidthWorld;
            const float minZ = view.overviewCenterXZ.y - halfHeightWorld;
            const float maxZ = view.overviewCenterXZ.y + halfHeightWorld;

            const ImU32 minorColor = IM_COL32(70, 82, 96, 105);
            const ImU32 axisColor = IM_COL32(116, 132, 150, 180);
            int lineCount = 0;
            for (float x = std::floor(minX / gridStep) * gridStep;
                 x <= maxX && lineCount < 256;
                 x += gridStep, ++lineCount) {
                const ImVec2 screen = WorldToScreen(
                    { x, view.overviewCenterXZ.y },
                    view.overviewCenterXZ,
                    canvasCenter,
                    pixelsPerUnit);
                const ImU32 color = std::abs(x) <= gridStep * 0.01f
                    ? axisColor
                    : minorColor;
                drawList.AddLine(
                    { screen.x, canvasMin.y },
                    { screen.x, canvasMax.y },
                    color,
                    std::abs(x) <= gridStep * 0.01f ? 1.5f : 1.0f);
            }

            lineCount = 0;
            for (float z = std::floor(minZ / gridStep) * gridStep;
                 z <= maxZ && lineCount < 256;
                 z += gridStep, ++lineCount) {
                const ImVec2 screen = WorldToScreen(
                    { view.overviewCenterXZ.x, z },
                    view.overviewCenterXZ,
                    canvasCenter,
                    pixelsPerUnit);
                const ImU32 color = std::abs(z) <= gridStep * 0.01f
                    ? axisColor
                    : minorColor;
                drawList.AddLine(
                    { canvasMin.x, screen.y },
                    { canvasMax.x, screen.y },
                    color,
                    std::abs(z) <= gridStep * 0.01f ? 1.5f : 1.0f);
            }
        }

        void DrawSceneObjectPoints(
            ImDrawList& drawList,
            const DocumentSceneBase& scene,
            const EditorViewInstance& view,
            const ImVec2& canvasCenter,
            float pixelsPerUnit) {

            for (const auto& object : scene.GetWorld().GetObjects()) {
                if (!object || object->GetComponent<CameraComponent>() != nullptr) {
                    continue;
                }
                const MATH::Vec3 position = ExtractAxis(
                    object->Transform().GetWorldMatrix(),
                    3);
                const ImVec2 screen = WorldToScreen(
                    { position.x, position.z },
                    view.overviewCenterXZ,
                    canvasCenter,
                    pixelsPerUnit);
                drawList.AddCircleFilled(
                    screen,
                    2.2f,
                    IM_COL32(155, 166, 178, 150),
                    8);
            }
        }

        void DrawCameraMarker(
            ImDrawList& drawList,
            const OverviewCameraEntry& camera,
            const EditorViewInstance& view,
            const ImVec2& canvasCenter,
            float pixelsPerUnit) {

            const ImVec2 position = WorldToScreen(
                camera.positionXZ,
                view.overviewCenterXZ,
                canvasCenter,
                pixelsPerUnit);
            const ImU32 color = ResolveCameraColor(camera);
            const float frustumWorldLength = std::clamp(
                camera.farClip * 0.08f,
                view.overviewUnitsPerScreen * 0.025f,
                view.overviewUnitsPerScreen * 0.14f);
            const float halfAngle = std::clamp(camera.fovYRad * 0.5f, 0.05f, 1.45f);
            const float cosine = std::cos(halfAngle);
            const float sine = std::sin(halfAngle);
            const MATH::Vec2 leftDirection{
                camera.forwardXZ.x * cosine - camera.forwardXZ.y * sine,
                camera.forwardXZ.x * sine + camera.forwardXZ.y * cosine
            };
            const MATH::Vec2 rightDirection{
                camera.forwardXZ.x * cosine + camera.forwardXZ.y * sine,
                -camera.forwardXZ.x * sine + camera.forwardXZ.y * cosine
            };
            const ImVec2 forwardEnd = WorldToScreen(
                camera.positionXZ + camera.forwardXZ * frustumWorldLength,
                view.overviewCenterXZ,
                canvasCenter,
                pixelsPerUnit);
            const ImVec2 leftEnd = WorldToScreen(
                camera.positionXZ + leftDirection * frustumWorldLength,
                view.overviewCenterXZ,
                canvasCenter,
                pixelsPerUnit);
            const ImVec2 rightEnd = WorldToScreen(
                camera.positionXZ + rightDirection * frustumWorldLength,
                view.overviewCenterXZ,
                canvasCenter,
                pixelsPerUnit);

            drawList.AddLine(position, leftEnd, color, 1.2f);
            drawList.AddLine(position, rightEnd, color, 1.2f);
            drawList.AddLine(leftEnd, rightEnd, color, 1.0f);
            drawList.AddLine(position, forwardEnd, color, 1.8f);
            drawList.AddCircleFilled(position, 5.0f, color, 12);
            if (camera.isDefault) {
                drawList.AddCircle(position, 8.0f, IM_COL32(105, 240, 164, 255), 16, 1.5f);
            }
            if (camera.isPreviewed) {
                drawList.AddCircle(position, 10.5f, IM_COL32(198, 142, 255, 255), 16, 1.4f);
            }

            std::string label = camera.name.empty()
                ? ("Camera " + std::to_string(camera.objectId.value))
                : camera.name;
            if (!camera.enabled) {
                label += " [Disabled]";
            }
            drawList.AddText(
                { position.x + 9.0f, position.y - 8.0f },
                color,
                label.c_str());
        }

        CameraOverviewPanelResult MakeAction(
            CameraOverviewActionKind kind,
            SceneObjectId cameraObjectId) {

            CameraOverviewPanelResult result{};
            result.action.kind = kind;
            result.action.cameraObjectId = cameraObjectId;
            return result;
        }

    } // namespace
#endif

    CameraOverviewPanelResult CameraOverviewPanel::DrawOverviewContents(
        DocumentSceneBase& scene,
        EditorContext& context,
        EditorViewInstance& view) const {

#if defined(HIKARI_WITH_EDITOR)
        CameraOverviewPanelResult result{};
        std::vector<OverviewCameraEntry> cameras = GatherCameras(scene, context);

        if (ImGui::Button("Fit Cameras")) {
            FitCameras(view, cameras);
        }
        ImGui::SameLine();
        ImGui::TextDisabled(
            "%zu cameras | Middle-drag pan | Wheel zoom",
            cameras.size());

        ImVec2 canvasSize = ImGui::GetContentRegionAvail();
        canvasSize.x = (std::max)(canvasSize.x, kMinCanvasSize);
        canvasSize.y = (std::max)(canvasSize.y, kMinCanvasSize);
        const ImVec2 canvasMin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton(
            "##CameraOverviewCanvas",
            canvasSize,
            ImGuiButtonFlags_MouseButtonLeft |
                ImGuiButtonFlags_MouseButtonMiddle);
        const ImVec2 canvasMax = ImGui::GetItemRectMax();
        const ImVec2 canvasCenter{
            (canvasMin.x + canvasMax.x) * 0.5f,
            (canvasMin.y + canvasMax.y) * 0.5f
        };

        view.extent.width = ToExtentDimension(canvasSize.x);
        view.extent.height = ToExtentDimension(canvasSize.y);
        view.interaction.visible = ImGui::IsItemVisible();
        view.interaction.focused =
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
        view.interaction.hovered = ImGui::IsItemHovered();
        view.interaction.gizmoCaptured = false;
        view.interaction.keyboardActive = false;
        view.overviewUnitsPerScreen = std::clamp(
            view.overviewUnitsPerScreen,
            kMinOverviewUnits,
            kMaxOverviewUnits);

        float pixelsPerUnit = canvasSize.x / view.overviewUnitsPerScreen;
        if (view.interaction.hovered &&
            ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
            const ImVec2 delta = ImGui::GetIO().MouseDelta;
            view.overviewCenterXZ.x -= delta.x / pixelsPerUnit;
            view.overviewCenterXZ.y += delta.y / pixelsPerUnit;
        }

        const float mouseWheel = view.interaction.hovered
            ? ImGui::GetIO().MouseWheel
            : 0.0f;
        if (mouseWheel != 0.0f) {
            const ImVec2 mousePosition = ImGui::GetMousePos();
            const MATH::Vec2 cursorWorld = ScreenToWorld(
                mousePosition,
                view.overviewCenterXZ,
                canvasCenter,
                pixelsPerUnit);
            view.overviewUnitsPerScreen = std::clamp(
                view.overviewUnitsPerScreen * std::pow(0.85f, mouseWheel),
                kMinOverviewUnits,
                kMaxOverviewUnits);
            pixelsPerUnit = canvasSize.x / view.overviewUnitsPerScreen;
            view.overviewCenterXZ.x =
                cursorWorld.x - (mousePosition.x - canvasCenter.x) / pixelsPerUnit;
            view.overviewCenterXZ.y =
                cursorWorld.y + (mousePosition.y - canvasCenter.y) / pixelsPerUnit;
        }

        const bool cameraSelectionClicked =
            view.interaction.hovered &&
            ImGui::IsItemClicked(ImGuiMouseButton_Left);
        view.interaction.mouseCaptured =
            view.interaction.hovered &&
            (ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
                ImGui::IsMouseDown(ImGuiMouseButton_Middle));

        if (cameraSelectionClicked) {
            const ImVec2 mousePosition = ImGui::GetMousePos();
            const OverviewCameraEntry* nearestCamera = nullptr;
            float nearestDistanceSquared = kCameraHitRadius * kCameraHitRadius;
            for (const OverviewCameraEntry& camera : cameras) {
                const ImVec2 screen = WorldToScreen(
                    camera.positionXZ,
                    view.overviewCenterXZ,
                    canvasCenter,
                    pixelsPerUnit);
                const float dx = screen.x - mousePosition.x;
                const float dy = screen.y - mousePosition.y;
                const float distanceSquared = dx * dx + dy * dy;
                if (distanceSquared <= nearestDistanceSquared) {
                    nearestDistanceSquared = distanceSquared;
                    nearestCamera = &camera;
                }
            }
            if (nearestCamera) {
                SelectRuntimeObject(
                    context,
                    FindRuntimeObject(scene, nearestCamera->objectId));
                result = MakeAction(
                    CameraOverviewActionKind::Select,
                    nearestCamera->objectId);
                cameras = GatherCameras(scene, context);
            }
        }

        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->PushClipRect(canvasMin, canvasMax, true);
        drawList->AddRectFilled(
            canvasMin,
            canvasMax,
            IM_COL32(18, 23, 30, 255));
        DrawGrid(
            *drawList,
            canvasMin,
            canvasMax,
            canvasCenter,
            view,
            pixelsPerUnit);
        DrawSceneObjectPoints(
            *drawList,
            scene,
            view,
            canvasCenter,
            pixelsPerUnit);
        for (const OverviewCameraEntry& camera : cameras) {
            DrawCameraMarker(
                *drawList,
                camera,
                view,
                canvasCenter,
                pixelsPerUnit);
        }
        drawList->AddRect(
            canvasMin,
            canvasMax,
            IM_COL32(84, 101, 120, 220),
            3.0f,
            0,
            1.0f);
        drawList->PopClipRect();
        return result;
#else
        (void)scene;
        (void)context;
        (void)view;
        return {};
#endif
    }

    CameraOverviewPanelResult CameraOverviewPanel::DrawCameraListContents(
        DocumentSceneBase& scene,
        EditorContext& context) const {

#if defined(HIKARI_WITH_EDITOR)
        CameraOverviewPanelResult result{};
        std::vector<OverviewCameraEntry> cameras = GatherCameras(scene, context);

        ImGui::TextDisabled("%zu scene cameras", cameras.size());
        if (scene.IsEditorCameraPreviewActive()) {
            ImGui::SameLine();
            if (ImGui::Button("Exit Camera View")) {
                return MakeAction(
                    CameraOverviewActionKind::ExitView,
                    scene.GetEditorCameraPreviewObjectId());
            }
        }

        if (cameras.empty()) {
            ImGui::Separator();
            ImGui::TextDisabled("No CameraComponent exists in the current scene.");
            return result;
        }

        const ImGuiTableFlags tableFlags =
            ImGuiTableFlags_RowBg |
            ImGuiTableFlags_BordersInnerV |
            ImGuiTableFlags_SizingStretchProp |
            ImGuiTableFlags_ScrollY;
        if (!ImGui::BeginTable("CinematicsCameraList", 4, tableFlags)) {
            return result;
        }

        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Camera", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("State", ImGuiTableColumnFlags_WidthFixed, 110.0f);
        ImGui::TableSetupColumn("Lens", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 190.0f);
        ImGui::TableHeadersRow();

        for (const OverviewCameraEntry& camera : cameras) {
            const std::string cameraIdScope =
                "Camera:" + std::to_string(camera.objectId.value);
            ImGui::PushID(cameraIdScope.c_str());
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            const std::string label = camera.name.empty()
                ? ("Camera " + std::to_string(camera.objectId.value))
                : camera.name;
            if (ImGui::Selectable(
                    label.c_str(),
                    camera.isSelected,
                    ImGuiSelectableFlags_SpanAllColumns |
                        ImGuiSelectableFlags_AllowOverlap)) {
                SelectRuntimeObject(
                    context,
                    FindRuntimeObject(scene, camera.objectId));
                result = MakeAction(
                    CameraOverviewActionKind::Select,
                    camera.objectId);
            }

            ImGui::TableSetColumnIndex(1);
            if (!camera.enabled) {
                ImGui::TextDisabled("Disabled");
            } else if (camera.isPreviewed) {
                ImGui::TextColored(
                    ImVec4(0.78f, 0.56f, 1.0f, 1.0f),
                    "Preview");
            } else if (camera.isDefault) {
                ImGui::TextColored(
                    ImVec4(0.35f, 0.87f, 0.58f, 1.0f),
                    "Default");
            } else {
                ImGui::TextUnformatted("Ready");
            }

            ImGui::TableSetColumnIndex(2);
            ImGui::Text(
                "%.1f deg | %.3f / %.1f",
                camera.fovYRad * kRadiansToDegrees,
                camera.nearClip,
                camera.farClip);

            ImGui::TableSetColumnIndex(3);
            if (camera.isDefault) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SmallButton("Default")) {
                result = MakeAction(
                    CameraOverviewActionKind::SetDefault,
                    camera.objectId);
            }
            if (camera.isDefault) {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();
            if (!camera.enabled) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SmallButton(camera.isPreviewed ? "Exit" : "View")) {
                result = MakeAction(
                    camera.isPreviewed
                        ? CameraOverviewActionKind::ExitView
                        : CameraOverviewActionKind::ViewThrough,
                    camera.objectId);
            }
            if (!camera.enabled) {
                ImGui::EndDisabled();
            }

            ImGui::SameLine();
            if (camera.hasParent) {
                ImGui::BeginDisabled();
            }
            if (ImGui::SmallButton("Snap")) {
                result = MakeAction(
                    CameraOverviewActionKind::SnapToView,
                    camera.objectId);
            }
            if (camera.hasParent) {
                ImGui::EndDisabled();
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                    ImGui::SetTooltip("Snap is available for root Camera objects only.");
                }
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
        return result;
#else
        (void)scene;
        (void)context;
        return {};
#endif
    }

} // namespace HIKARI::EDITOR
