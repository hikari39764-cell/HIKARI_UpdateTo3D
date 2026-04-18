#include "HIKARI_EnvironmentPanel.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/HIKARI_SceneEnvironment.h"
#include "Render3D/HIKARI_SkyRenderer.h"
#include "Vfx/HIKARI_PostProfile.h"

#if defined(_DEBUG)
#include "imgui.h"
#include <cstring>
#include <utility>
#endif

namespace HIKARI {

#if defined(_DEBUG)
    namespace {
        bool DrawParamControl(const VFX::ParamDesc& param, DirectX::XMFLOAT4& slotValue) {
            float value[4] = { slotValue.x, slotValue.y, slotValue.z, slotValue.w };
            bool changed = false;
            const char* label = param.label.empty() ? param.key.c_str() : param.label.c_str();
            if (param.ref.channel >= 4) {
                return false;
            }
            switch (param.type) {
            case VFX::ParamType::Float:
                changed = ImGui::DragFloat(label, &value[param.ref.channel], param.speed, param.minValues[0], param.maxValues[0]);
                break;
            case VFX::ParamType::Float2:
                if (param.ref.channel > 2) break;
                changed = ImGui::DragFloat2(label, &value[param.ref.channel], param.speed, param.minValues[0], param.maxValues[0]);
                break;
            case VFX::ParamType::Float3:
                if (param.ref.channel > 1) break;
                changed = ImGui::DragFloat3(label, &value[param.ref.channel], param.speed, param.minValues[0], param.maxValues[0]);
                break;
            case VFX::ParamType::Float4:
                if (param.ref.channel > 0) break;
                changed = ImGui::DragFloat4(label, &value[param.ref.channel], param.speed, param.minValues[0], param.maxValues[0]);
                break;
            case VFX::ParamType::Color:
                if (param.ref.channel > 0) break;
                changed = ImGui::ColorEdit4(label, &value[param.ref.channel]);
                break;
            case VFX::ParamType::Toggle: {
                bool enabled = value[param.ref.channel] >= 0.5f;
                if (ImGui::Checkbox(label, &enabled)) {
                    value[param.ref.channel] = enabled ? 1.0f : 0.0f;
                    changed = true;
                }
                break;
            }
            default:
                break;
            }
            if (changed) {
                slotValue = { value[0], value[1], value[2], value[3] };
            }
            return changed;
        }
    }

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
            const std::string previousProfileId = environment.post.globalPostProfileId;
            char profileBuffer[256]{};
            std::strncpy(profileBuffer, environment.post.globalPostProfileId.c_str(), sizeof(profileBuffer) - 1);
            if (ImGui::InputText("Global Post Profile", profileBuffer, sizeof(profileBuffer))) {
                environment.post.globalPostProfileId = profileBuffer;
            }

            if (!environment.post.globalPostProfileId.empty()) {
                PostProfile profile{};
                if (PostProfile::LoadById(environment.post.globalPostProfileId, profile)) {
                    const bool profileChanged = (environment.post.globalPostProfileId != previousProfileId);
                    if (profileChanged || !environment.post.valuesInitialized) {
                        profile.CopyValuesTo(environment.post.paramValues);
                        environment.post.valuesInitialized = true;
                    }

                    if (ImGui::Button("Reset To Profile Defaults")) {
                        profile.CopyValuesTo(environment.post.paramValues);
                        environment.post.valuesInitialized = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Reload Profile")) {
                        PostProfile reloadedProfile{};
                        if (PostProfile::LoadById(environment.post.globalPostProfileId, reloadedProfile)) {
                            profile = std::move(reloadedProfile);
                        }
                    }

                    if (ImGui::TreeNode("Profile Parameters")) {
                        for (const VFX::ParamDesc& param : profile.params) {
                            const int slot = static_cast<int>(param.ref.slot);
                            if (slot < 0 || slot >= 16 || param.ref.channel >= 4) {
                                continue;
                            }
                            ImGui::PushID(param.key.c_str());
                            DrawParamControl(param, environment.post.paramValues[slot]);
                            ImGui::PopID();
                        }
                        ImGui::TreePop();
                    }
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "Profile not found: %s", environment.post.globalPostProfileId.c_str());
                }
            }

            if (ImGui::TreeNode("Advanced Raw Parameter Block (16x float4)")) {
                for (int i = 0; i < 16; ++i) {
                    ImGui::PushID(i);
                    ImGui::InputFloat4("Param", &environment.post.paramValues[i].x);
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
