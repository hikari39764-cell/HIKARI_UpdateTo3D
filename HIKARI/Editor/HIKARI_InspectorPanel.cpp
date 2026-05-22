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

        DrawContents(selection);

        ImGui::End();
#else
        (void)selection;
#endif
    }

    void InspectorPanel::DrawContents(EditorSelection& selection) const {
#if defined(_DEBUG)
        if (selection.selectedObject == nullptr) {
            ImGui::TextUnformatted("No object selected.");
            return;
        }

        GameObject& object = *selection.selectedObject;
        const std::string objectIdScope = "Object:" + std::to_string(object.GetDocumentId().value);
        ImGui::PushID(objectIdScope.c_str());
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
        const auto& components = object.GetComponents();
        for (size_t componentIndex = 0; componentIndex < components.size(); ++componentIndex) {
            const auto& component = components[componentIndex];
            if (!component) {
                continue;
            }
            const std::string label(component->GetTypeName());
            ImGui::PushID(static_cast<int>(componentIndex));
            if (ImGui::TreeNodeEx("Component", ImGuiTreeNodeFlags_DefaultOpen, "%s", label.c_str())) {
                component->BuildInspector(builder);
                component->RenderImGui();
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::PopID();
#else
        (void)selection;
#endif
    }

} // namespace HIKARI
