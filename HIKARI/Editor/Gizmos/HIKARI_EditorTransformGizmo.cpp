#include "Editor/Gizmos/HIKARI_EditorTransformGizmo.h"

#if defined(_DEBUG)
#include "imgui.h"
#include "ImGuizmo.h"
#endif

#include "Scene/HIKARI_GameObject.h"

#include <algorithm>
#include <cmath>

namespace HIKARI::EDITOR {

#if defined(_DEBUG)
    namespace {
        struct DecomposedGizmoMatrix {
            TransformData transform{};
            MATH::Quat rotation = MATH::Quat::Identity();
        };

        void CopyMat4ToFloat16(const MATH::Mat4& matrix, float out[16]) {
            for (int column = 0; column < 4; ++column) {
                for (int row = 0; row < 4; ++row) {
                    out[column * 4 + row] = matrix.m[column][row];
                }
            }
        }

        MATH::Vec3 NormalizeAxis(float x, float y, float z) {
            const float length = std::sqrt(x * x + y * y + z * z);
            if (length <= 1.0e-6f) {
                return { 0.0f, 0.0f, 0.0f };
            }

            const float invLength = 1.0f / length;
            return { x * invLength, y * invLength, z * invLength };
        }

        MATH::Quat ExtractRotationFromMatrix(const float matrix[16]) {
            const MATH::Vec3 right = NormalizeAxis(matrix[0], matrix[1], matrix[2]);
            const MATH::Vec3 up = NormalizeAxis(matrix[4], matrix[5], matrix[6]);
            const MATH::Vec3 dir = NormalizeAxis(matrix[8], matrix[9], matrix[10]);

            const float m00 = right.x;
            const float m01 = up.x;
            const float m02 = dir.x;
            const float m10 = right.y;
            const float m11 = up.y;
            const float m12 = dir.y;
            const float m20 = right.z;
            const float m21 = up.z;
            const float m22 = dir.z;

            MATH::Quat q{};
            const float trace = m00 + m11 + m22;
            if (trace > 0.0f) {
                const float s = std::sqrt(trace + 1.0f) * 2.0f;
                q.w = 0.25f * s;
                q.x = (m21 - m12) / s;
                q.y = (m02 - m20) / s;
                q.z = (m10 - m01) / s;
            } else if (m00 > m11 && m00 > m22) {
                const float s = std::sqrt(1.0f + m00 - m11 - m22) * 2.0f;
                q.w = (m21 - m12) / s;
                q.x = 0.25f * s;
                q.y = (m01 + m10) / s;
                q.z = (m02 + m20) / s;
            } else if (m11 > m22) {
                const float s = std::sqrt(1.0f + m11 - m00 - m22) * 2.0f;
                q.w = (m02 - m20) / s;
                q.x = (m01 + m10) / s;
                q.y = 0.25f * s;
                q.z = (m12 + m21) / s;
            } else {
                const float s = std::sqrt(1.0f + m22 - m00 - m11) * 2.0f;
                q.w = (m10 - m01) / s;
                q.x = (m02 + m20) / s;
                q.y = (m12 + m21) / s;
                q.z = 0.25f * s;
            }

            return MATH::NormalizeQ(q);
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

        DecomposedGizmoMatrix DecomposeEditedMatrix(const float matrix[16]) {
            float translation[3]{};
            float rotationDeg[3]{};
            float scale[3]{};
            ImGuizmo::DecomposeMatrixToComponents(matrix, translation, rotationDeg, scale);

            DecomposedGizmoMatrix decomposed{};
            decomposed.transform.position = { translation[0], translation[1], translation[2] };
            decomposed.transform.scale = {
                (std::max)(0.001f, scale[0]),
                (std::max)(0.001f, scale[1]),
                (std::max)(0.001f, scale[2])
            };
            // ImGuizmoのEuler値ではなく、行列から復元したQuaternionを基準に同期する。
            decomposed.rotation = ExtractRotationFromMatrix(matrix);
            decomposed.transform.rotationEulerDeg = MATH::EulerXYZDegreesFromQuat(decomposed.rotation);
            return decomposed;
        }
    }
#endif

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
            const DecomposedGizmoMatrix decomposed = DecomposeEditedMatrix(model);
            result.transform = decomposed.transform;
            result.rotation = decomposed.rotation;

            Transform3D& runtimeTransform = object.Transform();
            runtimeTransform.useExplicitMatrix = false;
            runtimeTransform.position = result.transform.position;
            runtimeTransform.scale = result.transform.scale;
            runtimeTransform.rotation = result.rotation;
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
