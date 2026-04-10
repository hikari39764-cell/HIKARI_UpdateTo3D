#include "HIKARI_InspectorPanel.h"
#include "HIKARI_EditorSelection.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#include "imgui.h"

namespace HIKARI {

    void InspectorPanel::Draw(EditorSelection& selection) const {
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
        ImGui::DragFloat4("Rotation (quat)", &transform.rotation.x, 0.01f);
        ImGui::DragFloat3("Scale", &transform.scale.x, 0.01f, 0.001f, 1000.0f);

        ImGui::SeparatorText("Components");
        for (const auto& component : object.GetComponents()) {
            if (ImGui::TreeNode(component->GetTypeName())) {
                component->RenderImGui();
                ImGui::TreePop();
            }
        }

        ImGui::End();
    }

} // namespace HIKARI
