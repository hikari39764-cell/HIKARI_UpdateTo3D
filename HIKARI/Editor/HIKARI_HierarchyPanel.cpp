#include "HIKARI_HierarchyPanel.h"
#include "HIKARI_EditorSelection.h"
#include "Editor/Style/HIKARI_EditorIconManager.h"
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
        const auto& objects = world.GetObjects();
        ImGui::TextDisabled("%d objects", static_cast<int>(objects.size()));
        ImGui::Separator();

        if (objects.empty()) {
            ImGui::TextDisabled("No scene objects");
            return;
        }

        for (const auto& object : objects) {
            GameObject* objectPtr = object.get();
            ImGui::PushID(objectPtr);
            const bool isSelected = (selection.selectedObject == objectPtr);
            EDITOR::EditorIconManager::DrawIcon(EDITOR::EditorIconKind::GameObject, ImVec2(16.0f, 16.0f));
            ImGui::SameLine();
            if (ImGui::Selectable(objectPtr->GetName().c_str(), isSelected)) {
                selection.selectedObject = objectPtr;
                selection.selectedAsset = nullptr;
                selection.selectedAssetGuid.clear();
                selection.selectedAssetPath.clear();
            }
            ImGui::PopID();
        }
#else
        (void)world;
        (void)selection;
#endif
    }

} // namespace HIKARI
