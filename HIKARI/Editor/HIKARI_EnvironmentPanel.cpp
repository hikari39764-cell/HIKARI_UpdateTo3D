#include "HIKARI_EnvironmentPanel.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/HIKARI_SceneEnvironment.h"

#if defined(_DEBUG)
#include "imgui.h"
#include <cstring>
#endif

namespace HIKARI {

#if defined(_DEBUG)
    void EnvironmentPanel::Draw(SceneEnvironment& environment) const {
        if (!ImGui::Begin("Environment")) {
            ImGui::End();
            return;
        }

        if (ImGui::CollapsingHeader("Lighting", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit3("Ambient Color", &environment.ambient.color.x);
            ImGui::DragFloat("Ambient Intensity", &environment.ambient.intensity, 0.01f, 0.0f, 10.0f);

            ImGui::SeparatorText("Directional");
            ImGui::Checkbox("Directional Enabled", &environment.directional.enabled);
            ImGui::DragFloat3("Directional Direction", &environment.directional.direction.x, 0.01f);
            environment.directional.direction = MATH::Normalize(environment.directional.direction);
            ImGui::ColorEdit3("Directional Color", &environment.directional.color.x);
            ImGui::DragFloat("Directional Intensity", &environment.directional.intensity, 0.01f, 0.0f, 20.0f);

            ImGui::SeparatorText("Specular");
            ImGui::DragFloat("Specular Intensity", &environment.specularIntensity, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Specular Power", &environment.specularPower, 1.0f, 1.0f, 256.0f);

            ImGui::SeparatorText("Point Lights");
            if (environment.pointLights.size() < 4 && ImGui::Button("Add Point Light")) {
                environment.pointLights.push_back(PointLight{});
            }
            for (size_t i = 0; i < environment.pointLights.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                PointLight& pointLight = environment.pointLights[i];
                if (ImGui::TreeNode("PointLight", "Point Light %zu", i)) {
                    ImGui::Checkbox("Enabled", &pointLight.enabled);
                    ImGui::DragFloat3("Position", &pointLight.position.x, 0.02f);
                    ImGui::DragFloat("Range", &pointLight.range, 0.05f, 0.05f, 100.0f);
                    ImGui::ColorEdit3("Color", &pointLight.color.x);
                    ImGui::DragFloat("Intensity", &pointLight.intensity, 0.01f, 0.0f, 20.0f);
                    if (ImGui::Button("Remove")) {
                        environment.pointLights.erase(environment.pointLights.begin() + static_cast<long long>(i));
                        ImGui::TreePop();
                        ImGui::PopID();
                        break;
                    }
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
        }

        if (ImGui::CollapsingHeader("Sky", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Sky Enabled", &environment.sky.enabled);
            char meshAssetBuffer[256]{};
            std::strncpy(meshAssetBuffer, environment.sky.meshAsset.c_str(), sizeof(meshAssetBuffer) - 1);
            if (ImGui::InputText("Sky Mesh Asset", meshAssetBuffer, sizeof(meshAssetBuffer))) {
                environment.sky.meshAsset = meshAssetBuffer;
            }
            char texturePathBuffer[512]{};
            std::strncpy(texturePathBuffer, environment.sky.texturePath.c_str(), sizeof(texturePathBuffer) - 1);
            if (ImGui::InputText("Sky Texture Path", texturePathBuffer, sizeof(texturePathBuffer))) {
                environment.sky.texturePath = texturePathBuffer;
            }
            ImGui::DragFloat("Sky Yaw", &environment.sky.yaw, 0.01f);
            ImGui::DragFloat("Sky Exposure", &environment.sky.exposure, 0.01f, 0.0f, 16.0f);
            ImGui::ColorEdit3("Sky Tint", &environment.sky.tint.x);
        }

        if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Show Light Debug", &environment.showLightDebug);
            ImGui::Checkbox("Show Point Light Markers", &environment.showPointLightMarkers);
            ImGui::Checkbox("Show Sky Debug Info", &environment.showSkyDebugInfo);
        }

        if (ImGui::Button("Reset Environment Defaults")) {
            environment = SceneEnvironment{};
            environment.directional.direction = MATH::Normalize(environment.directional.direction);
        }

        ImGui::End();
    }
#else
    void EnvironmentPanel::Draw(SceneEnvironment&) const {}
#endif

} // namespace HIKARI
