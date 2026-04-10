#include "HIKARI_HierarchyPanel.h"
#include "HIKARI_EditorSelection.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#include "imgui.h"

namespace HIKARI {

    void HierarchyPanel::Draw(World& world, EditorSelection& selection) const {
        if (!ImGui::Begin("Scene Hierarchy")) {
            ImGui::End();
            return;
        }

        for (const auto& object : world.GetObjects()) {
            GameObject* objectPtr = object.get();
            const bool isSelected = (selection.selectedObject == objectPtr);
            if (ImGui::Selectable(objectPtr->GetName().c_str(), isSelected)) {
                selection.selectedObject = objectPtr;
            }
        }

        ImGui::End();
    }

} // namespace HIKARI
