#include "HIKARI_LightingPanel.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/HIKARI_SceneLighting.h"
#include "imgui.h"

namespace HIKARI {

    void LightingPanel::Draw(SceneLighting& lighting) const {
        if (!ImGui::Begin("Lighting")) {
            ImGui::End();
            return;
        }

        ImGui::DragFloat3("Light Direction", &lighting.directionalDir.x, 0.01f);
        lighting.directionalDir = MATH::Normalize(lighting.directionalDir);
        ImGui::Text("normalized light dir: (%.3f, %.3f, %.3f)", lighting.directionalDir.x, lighting.directionalDir.y, lighting.directionalDir.z);
        ImGui::ColorEdit3("Directional Color", &lighting.directionalColor.x);
        ImGui::DragFloat("Directional Intensity", &lighting.directionalIntensity, 0.01f, 0.0f, 20.0f);

        ImGui::Separator();
        ImGui::ColorEdit3("Ambient Color", &lighting.ambientColor.x);
        ImGui::DragFloat("Ambient Intensity", &lighting.ambientIntensity, 0.01f, 0.0f, 10.0f);

        ImGui::Separator();
        ImGui::ColorEdit3("Specular Color", &lighting.specularColor.x);
        ImGui::DragFloat("Specular Intensity", &lighting.specularIntensity, 0.01f, 0.0f, 10.0f);
        ImGui::DragFloat("Specular Power", &lighting.specularPower, 1.0f, 1.0f, 256.0f);

        ImGui::Separator();
        if (ImGui::Button("Reset Lighting Defaults")) {
            lighting = SceneLighting{};
            lighting.directionalDir = MATH::Normalize(lighting.directionalDir);
        }

        ImGui::End();
    }

} // namespace HIKARI
