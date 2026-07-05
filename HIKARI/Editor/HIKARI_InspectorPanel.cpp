#include "HIKARI_InspectorPanel.h"
#include "Editor/Inspectors/HIKARI_ImGuiInspectorBuilder.h"
#include <string>
#include "Render3D/HIKARI_Math3D.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/Components/HIKARI_ModelComponent.h"
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {
    namespace {
        struct RotationEditorState {
            GameObject* object = nullptr;
            MATH::Vec3 eulerDeg{ 0.0f, 0.0f, 0.0f };
            bool editing = false;
        };

        RotationEditorState gRotationEditor{};

        MATH::Quat QuatFromEulerDegrees(const MATH::Vec3& eulerDeg) {
            constexpr float kDegToRad = 3.1415926535f / 180.0f;
            return MATH::Quat::FromEulerXYZ(
                eulerDeg.x * kDegToRad,
                eulerDeg.y * kDegToRad,
                eulerDeg.z * kDegToRad);
        }
    }

    void InspectorPanel::Draw(
        EditorSelection& selection,
        AssetRegistry* assetRegistry,
        AssetDatabase* assetDatabase) const {
#if defined(HIKARI_WITH_EDITOR)
        if (!ImGui::Begin("Inspector")) {
            ImGui::End();
            return;
        }

        DrawContents(selection, assetRegistry, assetDatabase);

        ImGui::End();
#else
        (void)selection;
        (void)assetRegistry;
        (void)assetDatabase;
#endif
    }

    void InspectorPanel::DrawContents(
        EditorSelection& selection,
        AssetRegistry* assetRegistry,
        AssetDatabase* assetDatabase) const {
#if defined(HIKARI_WITH_EDITOR)
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

        const MATH::Vec3 runtimeEulerDeg = MATH::EulerXYZDegreesFromQuat(transform.rotation);
        if (gRotationEditor.object != &object) {
            gRotationEditor.object = &object;
            gRotationEditor.eulerDeg = runtimeEulerDeg;
            gRotationEditor.editing = false;
        } else if (!gRotationEditor.editing) {
            // Gizmo邵ｺ・ｪ邵ｺ・ｩ陞溷､慚夊ｬｫ蝣ｺ・ｽ諛翫・Quaternion郢ｧ譽嗜spector髯ｦ・ｨ驕会ｽｺ邵ｺ・ｸ陷ｿ閧ｴ荳千ｸｺ蜷ｶ・狗ｸｲ繝ｻ            gRotationEditor.eulerDeg = runtimeEulerDeg;
        }
        if (ImGui::DragFloat3("Rotation Euler (deg)", &gRotationEditor.eulerDeg.x, 0.1f)) {
            transform.rotation = QuatFromEulerDegrees(gRotationEditor.eulerDeg);
        }
        gRotationEditor.editing = ImGui::IsItemActive();
        ImGui::DragFloat3("Scale", &transform.scale.x, 0.01f, 0.001f, 1000.0f);

        ImGui::SeparatorText("Components");
        ImGuiInspectorBuilder builder{};
        builder.SetContext(InspectorContext{
            assetRegistry,
            assetDatabase
        });
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
        (void)assetRegistry;
        (void)assetDatabase;
#endif
    }

} // namespace HIKARI
