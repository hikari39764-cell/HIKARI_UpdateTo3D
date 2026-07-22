#include "HIKARI_HierarchyPanel.h"
#include "Editor/HIKARI_EditorContext.h"
#include "Editor/Style/HIKARI_EditorIconManager.h"
#include "Scene/HIKARI_GameObject.h"
#include "Scene/HIKARI_World.h"
#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#endif

namespace HIKARI {

    void HierarchyPanel::Draw(World& world, EditorSelection& selection) const {
#if defined(HIKARI_WITH_EDITOR)
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
        DrawContents(world, selection, {});
    }

    void HierarchyPanel::DrawContents(
        World& world,
        EditorSelection& selection,
        const std::function<void(GameObject&)>&
            drawObjectContextMenu) const {
#if defined(HIKARI_WITH_EDITOR)
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
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
                selection.selectedObject = objectPtr;
                selection.selectedAsset = nullptr;
                selection.selectedAssetGuid.clear();
                selection.selectedAssetPath.clear();
            }
            if (drawObjectContextMenu &&
                ImGui::BeginPopupContextItem("ObjectContextMenu")) {
                drawObjectContextMenu(*objectPtr);
                ImGui::EndPopup();
            }
            ImGui::PopID();
        }
#else
        (void)world;
        (void)selection;
        (void)drawObjectContextMenu;
#endif
    }

} // namespace HIKARI
