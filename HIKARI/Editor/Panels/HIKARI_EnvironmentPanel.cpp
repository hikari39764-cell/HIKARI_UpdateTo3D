#include "HIKARI_EnvironmentPanel.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Render3D/Shadow/HIKARI_ShadowMapRenderer.h"
#include "Vfx/Post/HIKARI_PostProfile.h"
#include "Vfx/Post/HIKARI_PostSystem.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneTransitionBus.h"
#include "Diagnostics/HIKARI_DebugLogBuffer.h"
#include "Gfx/HIKARI_D3D12DebugTools.h"
#include "HIKARI_Services.h"

#if defined(_DEBUG)
#include "imgui.h"
#include <algorithm>
#include <cmath>
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

        void NormalizeDirectionalLight(DirectionalLight& light) {
            light.direction = MATH::Normalize(light.direction);
            if (MATH::Length(light.direction) <= 1e-6f) {
                light.direction = MATH::Normalize(MATH::Vec3{ 0.4f, -1.0f, -0.6f });
            }
        }

        size_t CountUploadablePointLights(const SceneEnvironment& environment) {
            size_t count = 0;
            for (const PointLight& pointLight : environment.pointLights) {
                if (pointLight.enabled && pointLight.range > 0.0f) {
                    ++count;
                }
            }
            return count;
        }

        constexpr float kPi = 3.14159265358979323846f;

        float RadToDeg(float radians) {
            return radians * 180.0f / kPi;
        }

        float DegToRad(float degrees) {
            return degrees * kPi / 180.0f;
        }

        MATH::Vec3 DirectionFromYawPitch(float yawDeg, float pitchDeg) {
            const float yaw = DegToRad(yawDeg);
            const float pitch = DegToRad(std::clamp(pitchDeg, -89.0f, 89.0f));
            const float cp = std::cos(pitch);
            return MATH::Normalize(MATH::Vec3{
                std::sin(yaw) * cp,
                -std::sin(pitch),
                std::cos(yaw) * cp
            });
        }

        void YawPitchFromDirection(const MATH::Vec3& direction, float& yawDeg, float& pitchDeg) {
            const MATH::Vec3 dir = MATH::Normalize(direction);
            yawDeg = RadToDeg(std::atan2(dir.x, dir.z));
            pitchDeg = RadToDeg(std::asin(std::clamp(-dir.y, -1.0f, 1.0f)));
        }

        const char* DebugViewName(RenderDebugView view) {
            switch (view) {
            case RenderDebugView::Normal: return "Normal";
            case RenderDebugView::Tangent: return "Tangent";
            case RenderDebugView::LightingOnly: return "Lighting Only";
            case RenderDebugView::None:
            default: return "None";
            }
        }

        void ApplyEnvironmentPreset(SceneEnvironment& environment, int presetIndex) {
            switch (presetIndex) {
            case 1: // Bright Day
                environment.directional.color = { 1.0f, 0.96f, 0.88f };
                environment.directional.intensity = 1.6f;
                environment.ambient.color = { 0.78f, 0.86f, 1.0f };
                environment.ambient.intensity = 0.35f;
                environment.bloom.enabled = true;
                environment.bloom.intensity = 0.35f;
                environment.fog.enabled = false;
                environment.toneMapping.exposure = 1.0f;
                environment.directionalShadow.strength = 0.65f;
                break;
            case 2: // Sunset
                environment.directional.color = { 1.0f, 0.58f, 0.32f };
                environment.directional.intensity = 1.2f;
                environment.ambient.color = { 0.35f, 0.35f, 0.65f };
                environment.ambient.intensity = 0.25f;
                environment.fog.enabled = true;
                environment.fog.color = { 0.9f, 0.5f, 0.35f };
                environment.fog.density = 0.015f;
                environment.bloom.enabled = true;
                environment.bloom.intensity = 0.7f;
                environment.toneMapping.exposure = 1.1f;
                environment.directionalShadow.strength = 0.7f;
                break;
            case 3: // Night
                environment.directional.color = { 0.45f, 0.55f, 1.0f };
                environment.directional.intensity = 0.25f;
                environment.ambient.color = { 0.08f, 0.1f, 0.18f };
                environment.ambient.intensity = 0.18f;
                environment.bloom.enabled = true;
                environment.bloom.intensity = 0.9f;
                environment.fog.enabled = true;
                environment.fog.color = { 0.05f, 0.07f, 0.13f };
                environment.fog.density = 0.01f;
                environment.toneMapping.exposure = 1.2f;
                environment.directionalShadow.strength = 0.45f;
                break;
            case 4: // Overcast
                environment.directional.color = { 0.85f, 0.9f, 1.0f };
                environment.directional.intensity = 0.65f;
                environment.ambient.color = { 0.7f, 0.75f, 0.82f };
                environment.ambient.intensity = 0.5f;
                environment.bloom.intensity = 0.2f;
                environment.fog.enabled = true;
                environment.fog.color = { 0.65f, 0.7f, 0.75f };
                environment.fog.density = 0.012f;
                environment.toneMapping.exposure = 0.95f;
                environment.directionalShadow.strength = 0.35f;
                break;
            case 5: // Stylized Blue
                environment.directional.color = { 0.65f, 0.85f, 1.0f };
                environment.directional.intensity = 1.1f;
                environment.ambient.color = { 0.18f, 0.28f, 0.55f };
                environment.ambient.intensity = 0.35f;
                environment.bloom.enabled = true;
                environment.bloom.intensity = 0.8f;
                environment.toneMapping.exposure = 1.15f;
                break;
            case 6: // Warm Indoor
                environment.directional.color = { 1.0f, 0.78f, 0.5f };
                environment.directional.intensity = 0.75f;
                environment.ambient.color = { 1.0f, 0.72f, 0.45f };
                environment.ambient.intensity = 0.32f;
                environment.bloom.intensity = 0.45f;
                environment.fog.enabled = false;
                environment.toneMapping.exposure = 1.05f;
                break;
            case 0:
            default:
                environment = SceneEnvironment{};
                break;
            }
            NormalizeDirectionalLight(environment.directional);
        }
    }

    void EnvironmentPanel::Draw(SceneEnvironment& environment, const SKYRENDERER::SkyRendererDebugState* skyDebugState) const {
        if (!ImGui::Begin("Environment")) {
            ImGui::End();
            return;
        }

        ImGui::SeparatorText("Scene Environment");

        if (ImGui::TreeNodeEx("Quick Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat("Exposure", &environment.toneMapping.exposure, 0.01f, 0.0f, 8.0f);
            ImGui::DragFloat("Sun Intensity", &environment.directional.intensity, 0.01f, 0.0f, 20.0f);
            ImGui::DragFloat("Ambient Intensity", &environment.ambient.intensity, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Bloom Intensity", &environment.bloom.intensity, 0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Fog Amount", &environment.fog.density, 0.001f, 0.0f, 1.0f);
            ImGui::DragFloat("Shadow Strength", &environment.directionalShadow.strength, 0.01f, 0.0f, 1.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Presets")) {
            const char* presets[] = {
                "Default",
                "Bright Day",
                "Sunset",
                "Night",
                "Overcast",
                "Stylized Blue",
                "Warm Indoor",
            };
            for (int i = 0; i < static_cast<int>(sizeof(presets) / sizeof(presets[0])); ++i) {
                ImGui::PushID(i);
                if (ImGui::Button(presets[i])) {
                    ApplyEnvironmentPreset(environment, i);
                }
                if ((i % 3) != 2) {
                    ImGui::SameLine();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Ambient", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit3("Ambient Color", &environment.ambient.color.x);
            ImGui::DragFloat("Ambient Intensity", &environment.ambient.intensity, 0.01f, 0.0f, 10.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Directional Enabled", &environment.directional.enabled);
            float yawDeg = 0.0f;
            float pitchDeg = 0.0f;
            YawPitchFromDirection(environment.directional.direction, yawDeg, pitchDeg);
            bool angleChanged = false;
            angleChanged |= ImGui::DragFloat("Sun Yaw", &yawDeg, 0.5f, -180.0f, 180.0f);
            angleChanged |= ImGui::DragFloat("Sun Pitch", &pitchDeg, 0.5f, -89.0f, 89.0f);
            if (angleChanged) {
                environment.directional.direction = DirectionFromYawPitch(yawDeg, pitchDeg);
            }
            if (ImGui::DragFloat3("Direction", &environment.directional.direction.x, 0.01f, -1.0f, 1.0f)) {
                NormalizeDirectionalLight(environment.directional);
            }
            NormalizeDirectionalLight(environment.directional);
            ImGui::ColorEdit3("Color", &environment.directional.color.x);
            ImGui::DragFloat("Intensity", &environment.directional.intensity, 0.01f, 0.0f, 20.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Directional Shadow", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Shadow Enabled", &environment.directionalShadow.enabled);
            int shadowQuality = environment.directionalShadow.resolution <= 1024 ? 0 : (environment.directionalShadow.resolution >= 4096 ? 2 : 1);
            const char* qualityNames[] = { "Low", "Medium", "High" };
            if (ImGui::Combo("Shadow Quality", &shadowQuality, qualityNames, static_cast<int>(sizeof(qualityNames) / sizeof(qualityNames[0])))) {
                environment.directionalShadow.resolution = shadowQuality == 0 ? 1024u : (shadowQuality == 2 ? 4096u : 2048u);
            }
            ImGui::DragFloat("Shadow Softness", &environment.directionalShadow.pcfRadius, 0.05f, 0.0f, 4.0f);
            ImGui::DragFloat("Shadow Range", &environment.directionalShadow.orthoSize, 0.1f, 1.0f, 200.0f);
            float acneFix = std::max(environment.directionalShadow.depthBias * 1000.0f, environment.directionalShadow.normalBias * 25.0f);
            if (ImGui::DragFloat("Shadow Acne Fix", &acneFix, 0.01f, 0.0f, 10.0f)) {
                environment.directionalShadow.depthBias = acneFix * 0.001f;
                environment.directionalShadow.normalBias = acneFix * 0.04f;
            }
            if (ImGui::TreeNode("Advanced Shadow Parameters")) {
            const int resolutions[] = { 1024, 2048, 4096 };
            int currentResolution = static_cast<int>(environment.directionalShadow.resolution);
            if (currentResolution != 1024 && currentResolution != 2048 && currentResolution != 4096) {
                currentResolution = 2048;
            }
            if (ImGui::BeginCombo("Resolution", std::to_string(currentResolution).c_str())) {
                for (int resolution : resolutions) {
                    const bool selected = currentResolution == resolution;
                    if (ImGui::Selectable(std::to_string(resolution).c_str(), selected)) {
                        environment.directionalShadow.resolution = static_cast<uint32_t>(resolution);
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::DragFloat("Ortho Size", &environment.directionalShadow.orthoSize, 0.1f, 1.0f, 200.0f);
            ImGui::DragFloat("Near Plane", &environment.directionalShadow.nearPlane, 0.01f, 0.001f, 50.0f);
            ImGui::DragFloat("Far Plane", &environment.directionalShadow.farPlane, 0.1f, 1.0f, 500.0f);
            ImGui::DragFloat("Depth Bias", &environment.directionalShadow.depthBias, 0.0001f, 0.0f, 0.05f, "%.5f");
            ImGui::DragFloat("Normal Bias", &environment.directionalShadow.normalBias, 0.001f, 0.0f, 1.0f, "%.4f");
            ImGui::DragFloat("Strength", &environment.directionalShadow.strength, 0.01f, 0.0f, 1.0f);
            ImGui::Checkbox("PCF Enabled", &environment.directionalShadow.pcfEnabled);
            ImGui::DragFloat("PCF Radius", &environment.directionalShadow.pcfRadius, 0.05f, 0.0f, 4.0f);
            ImGui::Checkbox("Stabilize", &environment.directionalShadow.stabilize);
            ImGui::Checkbox("Show Debug Texture", &environment.directionalShadow.showDebugTexture);
            ImGui::DragFloat("Shadow Distance", &environment.directionalShadow.shadowDistance, 0.1f, 1.0f, 200.0f);
            ImGui::Checkbox("Show Debug Frustum", &environment.directionalShadow.showDebugFrustum);
                ImGui::TreePop();
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Point Lights", ImGuiTreeNodeFlags_DefaultOpen)) {
            constexpr size_t kMaxUploadedPointLights = 8u;
            const size_t uploadableCount = CountUploadablePointLights(environment);
            const size_t uploadedPreviewCount = uploadableCount < kMaxUploadedPointLights ? uploadableCount : kMaxUploadedPointLights;
            ImGui::Text("Total: %zu  Uploadable: %zu / %zu", environment.pointLights.size(), uploadedPreviewCount, kMaxUploadedPointLights);
            if (uploadableCount > kMaxUploadedPointLights) {
                ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "Only first 8 enabled point lights are uploaded.");
            }
            if (ImGui::Button("Add Point Light")) {
                environment.pointLights.push_back(PointLight{});
            }
            for (size_t i = 0; i < environment.pointLights.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                PointLight& pointLight = environment.pointLights[i];
                if (ImGui::TreeNode("PointLight", "Point Light %zu", i)) {
                    ImGui::Checkbox("Enabled", &pointLight.enabled);
                    ImGui::DragFloat3("Position", &pointLight.position.x, 0.02f);
                    ImGui::DragFloat("Range", &pointLight.range, 0.05f, 0.0f, 100.0f);
                    ImGui::ColorEdit3("Color", &pointLight.color.x);
                    ImGui::DragFloat("Intensity", &pointLight.intensity, 0.01f, 0.0f, 20.0f);

                    if (ImGui::Button("Duplicate")) {
                        environment.pointLights.insert(environment.pointLights.begin() + static_cast<long long>(i + 1), pointLight);
                        ImGui::TreePop();
                        ImGui::PopID();
                        break;
                    }
                    ImGui::SameLine();
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
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Specular")) {
            ImGui::DragFloat("Specular Intensity", &environment.specularIntensity, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Specular Power", &environment.specularPower, 1.0f, 1.0f, 256.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Sky")) {
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
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Bloom")) {
            ImGui::Checkbox("Bloom Enabled", &environment.bloom.enabled);
            ImGui::DragFloat("Threshold", &environment.bloom.threshold, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Intensity", &environment.bloom.intensity, 0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Radius", &environment.bloom.radius, 0.01f, 0.0f, 8.0f);
            int downsampleCount = static_cast<int>(environment.bloom.downsampleCount);
            if (ImGui::SliderInt("Downsample Count", &downsampleCount, 1, 5)) {
                environment.bloom.downsampleCount = static_cast<uint32_t>(std::clamp(downsampleCount, 1, 5));
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Tone Mapping")) {
            ImGui::Checkbox("Tone Mapping Enabled", &environment.toneMapping.enabled);
            ImGui::DragFloat("Exposure", &environment.toneMapping.exposure, 0.01f, 0.0f, 8.0f);
            ImGui::DragFloat("Gamma", &environment.toneMapping.gamma, 0.01f, 0.1f, 4.0f);
            const char* modes[] = { "None", "Reinhard", "ACES Approx" };
            int mode = std::clamp(environment.toneMapping.mode, 0, 2);
            if (ImGui::Combo("Mode", &mode, modes, static_cast<int>(sizeof(modes) / sizeof(modes[0])))) {
                environment.toneMapping.mode = mode;
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Fog")) {
            ImGui::Checkbox("Fog Enabled", &environment.fog.enabled);
            ImGui::ColorEdit3("Fog Color", &environment.fog.color.x);
            ImGui::DragFloat("Density", &environment.fog.density, 0.001f, 0.0f, 1.0f);
            ImGui::DragFloat("Start Distance", &environment.fog.startDistance, 0.1f, 0.0f, 500.0f);
            ImGui::DragFloat("End Distance", &environment.fog.endDistance, 0.1f, 0.1f, 1000.0f);
            ImGui::DragFloat("Height Falloff", &environment.fog.heightFalloff, 0.001f, 0.0f, 2.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Debug")) {
            ImGui::Checkbox("Show Light Debug", &environment.showLightDebug);
            ImGui::Checkbox("Show Point Light Markers", &environment.showPointLightMarkers);
            ImGui::Checkbox("Show Sky Debug Info", &environment.showSkyDebugInfo);
            int debugView = static_cast<int>(environment.debugView);
            const char* debugViews[] = { "None", "Normal", "Tangent", "Lighting Only" };
            if (ImGui::Combo("Render Debug View", &debugView, debugViews, static_cast<int>(sizeof(debugViews) / sizeof(debugViews[0])))) {
                environment.debugView = static_cast<RenderDebugView>(std::clamp(debugView, 0, 3));
            }
            ImGui::Text("Active Debug View: %s", DebugViewName(environment.debugView));
            if (environment.debugView != RenderDebugView::None) {
                ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "Debug view overrides the final shaded output.");
            }

            const MESHRENDERER::MeshRendererDebugStats& lightStats = MESHRENDERER::GetDebugStats();
            ImGui::SeparatorText("Light Upload Stats");
            ImGui::Text("Directional Enabled: %s", lightStats.directionalEnabled ? "Yes" : "No");
            ImGui::Text("Directional Intensity: %.3f", lightStats.directionalIntensity);
            ImGui::Text("Ambient Intensity: %.3f", lightStats.ambientIntensity);
            ImGui::Text("Point Lights Total / Uploaded / Clamped: %zu / %zu / %zu",
                lightStats.pointLightTotalCount,
                lightStats.pointLightUploadedCount,
                lightStats.pointLightClampedCount);
            ImGui::Text("Specular Intensity / Power: %.3f / %.3f", lightStats.specularIntensity, lightStats.specularPower);
            ImGui::Text("Emissive Texture Cache Hit / Miss: %zu / %zu",
                lightStats.emissiveTextureCacheHitCount,
                lightStats.emissiveTextureCacheMissCount);
            ImGui::Text("Emissive Mapped / Fallback Primitives: %zu / %zu",
                lightStats.emissiveMappedPrimitiveCount,
                lightStats.emissiveMapFallbackCount);
            ImGui::Text("PBR / Unlit Primitives: %zu / %zu",
                lightStats.pbrPrimitiveCount,
                lightStats.unlitPrimitiveCount);
            ImGui::Text("MR Texture Cache Hit / Miss: %zu / %zu",
                lightStats.metallicRoughnessTextureCacheHitCount,
                lightStats.metallicRoughnessTextureCacheMissCount);
            ImGui::Text("MR Mapped / Fallback Primitives: %zu / %zu",
                lightStats.metallicRoughnessMappedPrimitiveCount,
                lightStats.metallicRoughnessFallbackCount);
            ImGui::Text("AO Texture Cache Hit / Miss: %zu / %zu",
                lightStats.occlusionTextureCacheHitCount,
                lightStats.occlusionTextureCacheMissCount);
            ImGui::Text("AO Mapped / Fallback Primitives: %zu / %zu",
                lightStats.occlusionMappedPrimitiveCount,
                lightStats.occlusionFallbackCount);

            const SHADOW::ShadowMapDebugStats& shadowStats = SHADOW::GetDebugStats();
            ImGui::SeparatorText("Shadow Map Stats");
            ImGui::Text("Shadow Enabled: %s", shadowStats.enabled ? "Yes" : "No");
            ImGui::Text("Resolution: %u", shadowStats.resolution);
            ImGui::Text("Casters Submitted: %zu", shadowStats.submittedCasterCount);
            ImGui::Text("Static / Skinned Draws: %zu / %zu", shadowStats.staticCasterDrawCount, shadowStats.skinnedCasterDrawCount);
            ImGui::Text("AlphaMask Draws: %zu", shadowStats.alphaMaskCasterDrawCount);
            ImGui::Text("Skipped No Cast Shadow: %zu", shadowStats.skippedNoCastShadowCount);
            ImGui::Text("Primitive Caster Draws: %zu", shadowStats.totalPrimitiveCasterDrawCount);
            ImGui::Text("Shadow Map Recreates: %zu", shadowStats.shadowMapRecreateCount);
            ImGui::Text("PCF: %s  Radius: %.2f", shadowStats.pcfEnabled != 0 ? "On" : "Off", shadowStats.pcfRadius);
            ImGui::Text("Bias / NormalBias: %.5f / %.4f", shadowStats.depthBias, shadowStats.normalBias);
            ImGui::Text("Ortho / Near / Far: %.2f / %.3f / %.2f", shadowStats.orthoSize, shadowStats.nearPlane, shadowStats.farPlane);
            ImGui::Text("Strength: %.2f", shadowStats.strength);
            if (environment.directionalShadow.showDebugTexture && SHADOW::IsDirectionalShadowEnabled()) {
                const D3D12_GPU_DESCRIPTOR_HANDLE shadowSrv = SHADOW::GetDirectionalShadowSrv();
                if (shadowSrv.ptr != 0) {
                    ImGui::SeparatorText("Shadow Map");
                    ImGui::Image(reinterpret_cast<ImTextureID>(shadowSrv.ptr), ImVec2(256.0f, 256.0f));
                }
            }

            const POST::PostSystem::BloomDebugStats& bloomStats = POST::PostSystem::GetBloomDebugStats();
            ImGui::SeparatorText("Bloom Stats");
            ImGui::Text("Enabled / Initialized / Failed: %s / %s / %s",
                bloomStats.enabled ? "Yes" : "No",
                bloomStats.initialized ? "Yes" : "No",
                bloomStats.failed ? "Yes" : "No");
            ImGui::Text("Pass Count: %u", bloomStats.passCount);
            ImGui::Text("Texture Size: %d x %d", bloomStats.textureWidth, bloomStats.textureHeight);
            ImGui::Text("Threshold / Intensity / Radius: %.2f / %.2f / %.2f",
                bloomStats.threshold,
                bloomStats.intensity,
                bloomStats.radius);
            ImGui::Text("Downsample Count: %u", bloomStats.downsampleCount);

            ImGui::SeparatorText("Render Diagnostics");
            if (ImGui::Button("Clear Render Errors")) {
                DEBUGLOG::ClearRenderErrors();
            }
            ImGui::SameLine();
            if (ImGui::Button("Dump PostSystem")) {
                POST::PostSystem::LogFrameState("EnvironmentPanel button");
            }
            ImGui::SameLine();
            if (ImGui::Button("Dump InfoQueue")) {
                GFX::DumpD3D12InfoQueue(SERVICES::gCtx.device, "EnvironmentPanel button");
            }
            const std::vector<std::string> recentErrors = DEBUGLOG::GetRecentRenderErrors(12);
            if (recentErrors.empty()) {
                ImGui::TextDisabled("No recent render errors.");
            } else if (ImGui::TreeNode("Recent Render Errors")) {
                for (const std::string& error : recentErrors) {
                    ImGui::TextWrapped("%s", error.c_str());
                    ImGui::Separator();
                }
                ImGui::TreePop();
            }

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
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Global Post")) {
                ImGui::Checkbox("Post Enabled", &environment.post.enabled);
                const std::string previousProfileId = environment.post.globalPostProfileId;
                char profileBuffer[256]{};
                std::strncpy(profileBuffer, environment.post.globalPostProfileId.c_str(), sizeof(profileBuffer) - 1);
                if (ImGui::InputText("Global Post Profile", profileBuffer, sizeof(profileBuffer))) {
                    environment.post.globalPostProfileId = profileBuffer;
                    if (environment.post.globalPostProfileId.empty()) {
                        environment.post.valuesInitialized = false;
                    }
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
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Transition Debug")) {
                const SceneTransitionBus* transitionBus = RuntimeSceneContext::GetTransitionBus();
                if (!transitionBus) {
                    ImGui::TextDisabled("Transition bus unavailable.");
                } else {
                    const TransitionVisualState visualState = transitionBus->GetVisualState();
                    const SceneTransitionBus::TransitionState state = transitionBus->GetState();

                    const char* stateLabel = "Unknown";
                    switch (state) {
                    case SceneTransitionBus::TransitionState::Idle: stateLabel = "Idle"; break;
                    case SceneTransitionBus::TransitionState::TransitionOut: stateLabel = "TransitionOut"; break;
                    case SceneTransitionBus::TransitionState::SwitchingScene: stateLabel = "SwitchingScene"; break;
                    case SceneTransitionBus::TransitionState::TransitionIn: stateLabel = "TransitionIn"; break;
                    default: break;
                    }

                    ImGui::Text("Profile Id: %s", visualState.profileId.empty() ? "<none>" : visualState.profileId.c_str());
                    ImGui::Text("State: %s", stateLabel);
                    ImGui::Text("Progress: %.3f", visualState.progress);
                    ImGui::Text("Out Duration: %.3f", visualState.outDuration);
                    ImGui::Text("In Duration: %.3f", visualState.inDuration);
                }
            ImGui::TreePop();
        }

        if (ImGui::Button("Reset Environment Defaults")) {
            environment = SceneEnvironment{};
            NormalizeDirectionalLight(environment.directional);
        }

        ImGui::End();
    }
#else
    void EnvironmentPanel::Draw(SceneEnvironment&, const SKYRENDERER::SkyRendererDebugState*) const {}
#endif

} // namespace HIKARI
