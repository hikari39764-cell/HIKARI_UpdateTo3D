#include "HIKARI_EnvironmentPanel.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/HIKARI_SceneEnvironment.h"
#include "Render3D/HIKARI_SkyRenderer.h"

#if defined(_DEBUG)
#include "imgui.h"
#include <cstring>
#endif

namespace HIKARI {

#if defined(_DEBUG)
    void EnvironmentPanel::Draw(SceneEnvironment& environment, const SKYRENDERER::SkyRendererDebugState* skyDebugState) const {
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
            if (environment.pointLights.size() < 8 && ImGui::Button("Add Point Light")) {
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
            char skyAssetBuffer[256]{};
            std::strncpy(skyAssetBuffer, environment.sky.skyAsset.c_str(), sizeof(skyAssetBuffer) - 1);
            if (ImGui::InputText("Sky Asset", skyAssetBuffer, sizeof(skyAssetBuffer))) {
                environment.sky.skyAsset = skyAssetBuffer;
            }
            ImGui::DragFloat("Sky Scale", &environment.sky.scale, 0.01f, 0.0001f, 1000.0f);
            ImGui::DragFloat("Sky Yaw", &environment.sky.yaw, 0.01f);
            ImGui::DragFloat("Sky Exposure", &environment.sky.exposure, 0.01f, 0.0f, 16.0f);
            ImGui::ColorEdit3("Sky Tint", &environment.sky.tint.x);
            ImGui::Checkbox("Follow Camera", &environment.sky.followCamera);
        }


        if (ImGui::CollapsingHeader("Post Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Post Enabled", &environment.post.enabled);
            char profileBuffer[256]{};
            std::strncpy(profileBuffer, environment.post.globalPostProfileId.c_str(), sizeof(profileBuffer) - 1);
            if (ImGui::InputText("Global Post Profile", profileBuffer, sizeof(profileBuffer))) {
                environment.post.globalPostProfileId = profileBuffer;
            }
            if (ImGui::TreeNode("User Overrides (16x float4)")) {
                for (int i = 0; i < 16; ++i) {
                    ImGui::PushID(i);
                    ImGui::InputFloat4("Override", &environment.post.userOverrides[i].x);
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }

        if (ImGui::CollapsingHeader("Debug", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Show Light Debug", &environment.showLightDebug);
            ImGui::Checkbox("Show Point Light Markers", &environment.showPointLightMarkers);
            ImGui::Checkbox("Show Sky Debug Info", &environment.showSkyDebugInfo);

            if (skyDebugState && environment.showSkyDebugInfo) {
                ImGui::SeparatorText("Sky Renderer State");
                ImGui::Text("Initialized: %s", skyDebugState->initialized ? "true" : "false");
                ImGui::Text("Render Submitted: %s", skyDebugState->lastRenderSubmitted ? "true" : "false");
                ImGui::Text("Sky Asset Found: %s", skyDebugState->skyAssetFound ? "true" : "false");
                ImGui::Text("Sky Mesh Loaded: %s", skyDebugState->skyMeshLoaded ? "true" : "false");
                ImGui::Text("Sky Mesh Valid: %s", skyDebugState->skyMeshValid ? "true" : "false");
                ImGui::Text("Texture Valid: %s", skyDebugState->textureValid ? "true" : "false");
                ImGui::Text("Active Sky Asset: %s", skyDebugState->activeSkyAsset.c_str());
                ImGui::Text("Active Texture: %s", skyDebugState->activeTexturePath.c_str());
            }
        }

        if (ImGui::Button("Reset Environment Defaults")) {
            environment = SceneEnvironment{};
            environment.directional.direction = MATH::Normalize(environment.directional.direction);
        }

        ImGui::End();
    }
#else
    void EnvironmentPanel::Draw(SceneEnvironment&, const SKYRENDERER::SkyRendererDebugState*) const {}
#endif

} // namespace HIKARI
