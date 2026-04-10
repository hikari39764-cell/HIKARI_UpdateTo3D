#include "HIKARI_StatsPanel.h"
#include "HIKARI_EditorSelection.h"
#include "Render3D/HIKARI_Camera3D.h"
#include "Render3D/HIKARI_ModelManager.h"
#include "Scene/HIKARI_World.h"
#include "imgui.h"
#include "Scene/HIKARI_GameObject.h"
namespace HIKARI {

    void StatsPanel::Draw(const char* sceneName, const World& world, const ModelManager& modelManager, const EditorSelection& selection, const Camera3D& camera) const {
        if (!ImGui::Begin("Scene / Render Stats")) {
            ImGui::End();
            return;
        }

        ImGui::Text("Scene: %s", sceneName ? sceneName : "<none>");
        ImGui::Text("World Objects: %zu", world.GetObjects().size());
        ImGui::Text("Model Assets: %zu", modelManager.GetAssets().size());
        ImGui::Text("Loaded Models: %zu", modelManager.CountLoadedAssets());
        ImGui::Text("Failed Models: %zu", modelManager.CountFailedAssets());
        ImGui::Text("Selected Object: %s", selection.selectedObject ? selection.selectedObject->GetName().c_str() : "<none>");
        ImGui::Text("Selected Asset: %s", selection.selectedAsset ? selection.selectedAsset->GetName().c_str() : "<none>");

        const MATH::Vec3 cameraPos = camera.GetPosition();
        ImGui::Text("Camera Pos: (%.2f, %.2f, %.2f)", cameraPos.x, cameraPos.y, cameraPos.z);
        ImGui::TextUnformatted("Debug Primitive Count: TODO");

        ImGui::End();
    }

} // namespace HIKARI
