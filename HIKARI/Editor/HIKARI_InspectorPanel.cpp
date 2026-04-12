#include "HIKARI_InspectorPanel.h"
#include "HIKARI_ImGuiInspectorBuilder.h"
#include <string>
#include "Render3D/HIKARI_Math3D.h"
#include "HIKARI_EditorSelection.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#if defined(_DEBUG)
#include "imgui.h"
#endif

namespace HIKARI {
    namespace {
        struct RotationEditorState {
            GameObject* object = nullptr;
            MATH::Vec3 eulerDeg{ 0.0f, 0.0f, 0.0f };
        };

        RotationEditorState gRotationEditor{};
    }

    void InspectorPanel::Draw(EditorSelection& selection) const {
#if defined(_DEBUG)
        if (!ImGui::Begin("Inspector")) {
            ImGui::End();
            return;
        }

        if (selection.selectedObject == nullptr) {
            ImGui::TextUnformatted("No object selected.");
            ImGui::End();
            return;
        }

        GameObject& object = *selection.selectedObject;
        ImGui::Text("Name: %s", object.GetName().c_str());

        Transform3D& transform = object.Transform();
        ImGui::SeparatorText("Transform3D");
        ImGui::DragFloat3("Position", &transform.position.x, 0.01f);
        if (gRotationEditor.object != &object) {
            gRotationEditor.object = &object;
            gRotationEditor.eulerDeg = { 0.0f, 0.0f, 0.0f };
        }
        if (ImGui::DragFloat3("Rotation Euler (deg)", &gRotationEditor.eulerDeg.x, 0.1f)) {
            constexpr float kDegToRad = 3.1415926535f / 180.0f;
            const float rx = gRotationEditor.eulerDeg.x * kDegToRad;
            const float ry = gRotationEditor.eulerDeg.y * kDegToRad;
            const float rz = gRotationEditor.eulerDeg.z * kDegToRad;
            transform.rotation = MATH::Quat::FromEulerXYZ(rx, ry, rz);
        }
        ImGui::DragFloat3("Scale", &transform.scale.x, 0.01f, 0.001f, 1000.0f);

        ImGui::SeparatorText("Components");
        ImGuiInspectorBuilder builder{};
        for (const auto& component : object.GetComponents()) {
            const std::string label(component->GetTypeName());
            if (ImGui::TreeNode(label.c_str())) {
                component->BuildInspector(builder);
                component->RenderImGui();
                ImGui::TreePop();
            }
        }

        ImGui::End();
#else
        (void)selection;
#endif
    }

} // namespace HIKARI
