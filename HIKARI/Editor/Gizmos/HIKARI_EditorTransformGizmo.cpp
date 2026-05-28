#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"

#if defined(_DEBUG)
#include "imgui.h"
#include "ImGuizmo.h"
#endif

#include "Scene/HIKARI_GameObject.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::EDITOR {

    namespace {
        constexpr float kDegToRad = 3.1415926535f / 180.0f;

#if defined(_DEBUG)
        void CopyMat4ToFloat16(const MATH::Mat4& matrix, float out[16]) {
            for (int column = 0; column < 4; ++column) {
                for (int row = 0; row < 4; ++row) {
                    out[column * 4 + row] = matrix.m[column][row];
                }
            }
        }

        ImGuizmo::OPERATION ToImGuizmoOperation(EditorTransformGizmoOperation operation) {
            switch (operation) {
            case EditorTransformGizmoOperation::Rotate:
                return ImGuizmo::ROTATE;
            case EditorTransformGizmoOperation::Scale:
                return ImGuizmo::SCALE;
            case EditorTransformGizmoOperation::Translate:
            default:
                return ImGuizmo::TRANSLATE;
            }
        }

        ImGuizmo::MODE ToImGuizmoMode(EditorTransformGizmoMode mode) {
            return mode == EditorTransformGizmoMode::Local
                ? ImGuizmo::LOCAL
                : ImGuizmo::WORLD;
        }

        void BuildSnapValues(
            const EditorTransformGizmoState& state,
            float outSnap[3]) {
            switch (state.operation) {
            case EditorTransformGizmoOperation::Rotate:
                outSnap[0] = state.rotateSnapDeg;
                outSnap[1] = state.rotateSnapDeg;
                outSnap[2] = state.rotateSnapDeg;
                break;
            case EditorTransformGizmoOperation::Scale:
                outSnap[0] = state.scaleSnap;
                outSnap[1] = state.scaleSnap;
                outSnap[2] = state.scaleSnap;
                break;
            case EditorTransformGizmoOperation::Translate:
            default:
                outSnap[0] = state.translateSnap.x;
                outSnap[1] = state.translateSnap.y;
                outSnap[2] = state.translateSnap.z;
                break;
            }
        }

        TransformData DecomposeEditedMatrix(const float matrix[16]) {
            float translation[3]{};
            float rotationDeg[3]{};
            float scale[3]{};
            ImGuizmo::DecomposeMatrixToComponents(matrix, translation, rotationDeg, scale);

            TransformData transform{};
            transform.position = { translation[0], translation[1], translation[2] };
            transform.rotationEulerDeg = { rotationDeg[0], rotationDeg[1], rotationDeg[2] };
            transform.scale = {
                (std::max)(0.001f, scale[0]),
                (std::max)(0.001f, scale[1]),
                (std::max)(0.001f, scale[2])
            };
            return transform;
        }
#endif
    }

    EditorTransformGizmoResult EditorTransformGizmo::Draw(
        GameObject& object,
        const Camera3D& camera,
        const EditorTransformGizmoState& state,
        const EditorViewportRect& viewportRect) const {
        EditorTransformGizmoResult result{};

#if defined(_DEBUG)
        if (!state.enabled || viewportRect.width <= 1.0f || viewportRect.height <= 1.0f) {
            return result;
        }

        float view[16]{};
        float projection[16]{};
        float model[16]{};
        CopyMat4ToFloat16(camera.GetView(), view);
        CopyMat4ToFloat16(camera.GetProj(), projection);
        CopyMat4ToFloat16(object.Transform().GetLocalMatrix(), model);

        ImGuizmo::BeginFrame();
        ImGuizmo::SetOrthographic(false);
        ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
        ImGuizmo::SetRect(viewportRect.x, viewportRect.y, viewportRect.width, viewportRect.height);

        float snap[3]{};
        const float* snapPtr = nullptr;
        if (state.snapEnabled) {
            BuildSnapValues(state, snap);
            snapPtr = snap;
        }

        // Transform 編集だけを担当し、保存形式への反映は呼び出し側に任せる。
        result.changed = ImGuizmo::Manipulate(
            view,
            projection,
            ToImGuizmoOperation(state.operation),
            ToImGuizmoMode(state.mode),
            model,
            nullptr,
            snapPtr);
        result.interacting = ImGuizmo::IsOver() || ImGuizmo::IsUsing();

        if (result.changed) {
            // TODO: undo stack に積む TransformEditCommand をここから生成する。
            result.transform = DecomposeEditedMatrix(model);
            Transform3D& runtimeTransform = object.Transform();
            runtimeTransform.useExplicitMatrix = false;
            runtimeTransform.position = result.transform.position;
            runtimeTransform.scale = result.transform.scale;
            runtimeTransform.rotation = MATH::Quat::FromEulerXYZ(
                result.transform.rotationEulerDeg.x * kDegToRad,
                result.transform.rotationEulerDeg.y * kDegToRad,
                result.transform.rotationEulerDeg.z * kDegToRad);
        }
#else
        (void)object;
        (void)camera;
        (void)state;
        (void)viewportRect;
#endif

        return result;
    }

} // namespace HIKARI::EDITOR
