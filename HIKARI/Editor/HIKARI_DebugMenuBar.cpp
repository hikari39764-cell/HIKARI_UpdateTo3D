#include "HIKARI_DebugMenuBar.h"
#include "HIKARI_DebugWindowState.h"
#include "Render3D/HIKARI_DebugCameraController3D.h"
#include "imgui.h"

namespace HIKARI {

    void DebugMenuBar::Draw(DebugWindowState& windows, DebugCameraController3D& debugCamera, bool& lightingEnabled) const {
        if (!ImGui::BeginMainMenuBar()) {
            return;
        }

        if (ImGui::BeginMenu("Windows")) {
            ImGui::MenuItem("Hierarchy", nullptr, &windows.showHierarchy);
            ImGui::MenuItem("Inspector", nullptr, &windows.showInspector);
            ImGui::MenuItem("Asset Browser", nullptr, &windows.showAssetBrowser);
            ImGui::MenuItem("Stats", nullptr, &windows.showStats);
            ImGui::MenuItem("Lighting", nullptr, &windows.showLighting);
            ImGui::MenuItem("Debug Camera", nullptr, &windows.showDebugCamera);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Scene")) {
            if (ImGui::MenuItem("Reset Camera")) {
                const MATH::Vec3 resetPos{ 0.0f, 2.0f, -6.0f };
                debugCamera.Reset(resetPos, 0.0f, 0.0f);
            }
            bool enabled = debugCamera.IsEnabled();
            if (ImGui::MenuItem("Toggle Debug Camera", nullptr, enabled)) {
                debugCamera.SetEnabled(!enabled);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Render")) {
            ImGui::MenuItem("Lighting Enabled", nullptr, &lightingEnabled);
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }

} // namespace HIKARI
