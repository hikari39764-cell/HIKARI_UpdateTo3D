#include "HIKARI_HierarchyPanel.h"
#include "HIKARI_EditorSelection.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#if defined(_DEBUG)
#include "imgui.h"
#endif

namespace HIKARI {

    void HierarchyPanel::Draw(World& world, EditorSelection& selection) const {
#if defined(_DEBUG)
        if (!ImGui::Begin("Scene Hierarchy")) {
            ImGui::End();
            return;
        }

        DrawContents(world, selection);

        ImGui::End();
#else
        (void)world;
        (void)selection;
#endif
    }

    void HierarchyPanel::DrawContents(World& world, EditorSelection& selection) const {
#if defined(_DEBUG)
        for (const auto& object : world.GetObjects()) {
            GameObject* objectPtr = object.get();
            ImGui::PushID(objectPtr);
            const bool isSelected = (selection.selectedObject == objectPtr);
            if (ImGui::Selectable(objectPtr->GetName().c_str(), isSelected)) {
                selection.selectedObject = objectPtr;
            }
            ImGui::PopID();
        }
#else
        (void)world;
        (void)selection;
#endif
    }

} // namespace HIKARI
