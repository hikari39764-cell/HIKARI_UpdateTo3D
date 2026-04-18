#include "HIKARI_DebugMenuBar.h"
#include "HIKARI_DebugWindowState.h"
#include "Render3D/Debug/HIKARI_DebugCameraController3D.h"

#if defined(_DEBUG)
#include "imgui.h"
#endif

namespace HIKARI {

#if defined(_DEBUG)
    void DebugMenuBar::Draw(DebugWindowState& windows, DebugCameraController3D& debugCamera, bool& environmentLightingEnabled) const {
        if (!ImGui::BeginMainMenuBar()) {
            return;
        }

        if (ImGui::BeginMenu("Windows")) {
            if (ImGui::BeginMenu("Scene Authoring")) {
                ImGui::MenuItem("Scene Workspace", nullptr, &windows.authoring.showSceneWorkspace);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Resources & Environment")) {
                ImGui::MenuItem("Asset Browser", nullptr, &windows.resources.showAssetBrowser);
                ImGui::MenuItem("Environment", nullptr, &windows.resources.showEnvironment);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Runtime & Debug")) {
                ImGui::MenuItem("Debug Workspace", nullptr, &windows.runtime.showDebugWorkspace);
                ImGui::EndMenu();
            }

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
            ImGui::MenuItem("Environment Lighting", nullptr, &environmentLightingEnabled);
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
    }
#else
    void DebugMenuBar::Draw(DebugWindowState&, DebugCameraController3D&, bool&) const {}
#endif

} // namespace HIKARI
