#include "HIKARI_EnvironmentPanel.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Render3D/Core/HIKARI_MeshRenderer.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_IblEnvironment.h"
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
#include <filesystem>
#include <utility>
#include <vector>
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

        bool EqualVec3(const MATH::Vec3& lhs, const MATH::Vec3& rhs) {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z;
        }

        bool EqualFloat4(const DirectX::XMFLOAT4& lhs, const DirectX::XMFLOAT4& rhs) {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
        }

        bool EqualSkySettings(const SkySettings& lhs, const SkySettings& rhs) {
            return lhs.enabled == rhs.enabled &&
                lhs.mode == rhs.mode &&
                lhs.skyAsset == rhs.skyAsset &&
                lhs.scale == rhs.scale &&
                lhs.yaw == rhs.yaw &&
                lhs.exposure == rhs.exposure &&
                EqualVec3(lhs.tint, rhs.tint) &&
                lhs.followCamera == rhs.followCamera &&
                EqualVec3(lhs.zenithColor, rhs.zenithColor) &&
                EqualVec3(lhs.horizonColor, rhs.horizonColor) &&
                EqualVec3(lhs.groundColor, rhs.groundColor) &&
                lhs.horizonPower == rhs.horizonPower &&
                lhs.showSunDisk == rhs.showSunDisk &&
                lhs.sunDiskIntensity == rhs.sunDiskIntensity &&
                lhs.sunDiskSize == rhs.sunDiskSize &&
                lhs.ambientFromSky == rhs.ambientFromSky &&
                lhs.reflectionIntensity == rhs.reflectionIntensity &&
                lhs.showDebugTexture == rhs.showDebugTexture;
        }

        bool EqualPointLight(const PointLight& lhs, const PointLight& rhs) {
            return lhs.enabled == rhs.enabled &&
                EqualVec3(lhs.position, rhs.position) &&
                lhs.range == rhs.range &&
                EqualVec3(lhs.color, rhs.color) &&
                lhs.intensity == rhs.intensity;
        }

        bool EqualPostSettings(const ScenePostSettings& lhs, const ScenePostSettings& rhs) {
            if (lhs.enabled != rhs.enabled ||
                lhs.globalPostProfileId != rhs.globalPostProfileId ||
                lhs.valuesInitialized != rhs.valuesInitialized) {
                return false;
            }
            for (int i = 0; i < 16; ++i) {
                if (!EqualFloat4(lhs.paramValues[i], rhs.paramValues[i])) {
                    return false;
                }
            }
            return true;
        }

        bool EqualSceneEnvironment(const SceneEnvironment& lhs, const SceneEnvironment& rhs) {
            if (!EqualVec3(lhs.ambient.color, rhs.ambient.color) ||
                lhs.ambient.intensity != rhs.ambient.intensity ||
                lhs.ambient.useSkyColor != rhs.ambient.useSkyColor ||
                lhs.ambient.skyBlend != rhs.ambient.skyBlend ||
                lhs.directional.enabled != rhs.directional.enabled ||
                !EqualVec3(lhs.directional.direction, rhs.directional.direction) ||
                lhs.directional.intensity != rhs.directional.intensity ||
                !EqualVec3(lhs.directional.color, rhs.directional.color) ||
                lhs.directionalShadow.enabled != rhs.directionalShadow.enabled ||
                lhs.directionalShadow.resolution != rhs.directionalShadow.resolution ||
                lhs.directionalShadow.orthoSize != rhs.directionalShadow.orthoSize ||
                lhs.directionalShadow.nearPlane != rhs.directionalShadow.nearPlane ||
                lhs.directionalShadow.farPlane != rhs.directionalShadow.farPlane ||
                lhs.directionalShadow.depthBias != rhs.directionalShadow.depthBias ||
                lhs.directionalShadow.normalBias != rhs.directionalShadow.normalBias ||
                lhs.directionalShadow.strength != rhs.directionalShadow.strength ||
                lhs.directionalShadow.pcfEnabled != rhs.directionalShadow.pcfEnabled ||
                lhs.directionalShadow.pcfRadius != rhs.directionalShadow.pcfRadius ||
                lhs.directionalShadow.stabilize != rhs.directionalShadow.stabilize ||
                lhs.directionalShadow.showDebugTexture != rhs.directionalShadow.showDebugTexture ||
                lhs.directionalShadow.shadowDistance != rhs.directionalShadow.shadowDistance ||
                lhs.directionalShadow.showDebugFrustum != rhs.directionalShadow.showDebugFrustum ||
                !EqualSkySettings(lhs.sky, rhs.sky) ||
                lhs.bloom.enabled != rhs.bloom.enabled ||
                lhs.bloom.threshold != rhs.bloom.threshold ||
                lhs.bloom.intensity != rhs.bloom.intensity ||
                lhs.bloom.radius != rhs.bloom.radius ||
                lhs.bloom.downsampleCount != rhs.bloom.downsampleCount ||
                lhs.fog.enabled != rhs.fog.enabled ||
                !EqualVec3(lhs.fog.color, rhs.fog.color) ||
                lhs.fog.density != rhs.fog.density ||
                lhs.fog.startDistance != rhs.fog.startDistance ||
                lhs.fog.endDistance != rhs.fog.endDistance ||
                lhs.fog.heightFalloff != rhs.fog.heightFalloff ||
                lhs.fog.useSkyHorizonColor != rhs.fog.useSkyHorizonColor ||
                lhs.toneMapping.enabled != rhs.toneMapping.enabled ||
                lhs.toneMapping.exposure != rhs.toneMapping.exposure ||
                lhs.toneMapping.gamma != rhs.toneMapping.gamma ||
                lhs.toneMapping.mode != rhs.toneMapping.mode ||
                lhs.debugView != rhs.debugView ||
                lhs.specularIntensity != rhs.specularIntensity ||
                lhs.specularPower != rhs.specularPower ||
                lhs.showLightDebug != rhs.showLightDebug ||
                lhs.showPointLightMarkers != rhs.showPointLightMarkers ||
                lhs.showSkyDebugInfo != rhs.showSkyDebugInfo ||
                !EqualPostSettings(lhs.post, rhs.post) ||
                lhs.pointLights.size() != rhs.pointLights.size()) {
                return false;
            }

            for (size_t i = 0; i < lhs.pointLights.size(); ++i) {
                if (!EqualPointLight(lhs.pointLights[i], rhs.pointLights[i])) {
                    return false;
                }
            }

            return true;
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
            case RenderDebugView::BaseColor: return "Base Color";
            case RenderDebugView::Roughness: return "Roughness";
            case RenderDebugView::Metallic: return "Metallic";
            case RenderDebugView::Occlusion: return "Occlusion";
            case RenderDebugView::Shadow: return "Shadow";
            case RenderDebugView::NdotL: return "NdotL";
            case RenderDebugView::Emissive: return "Emissive";
            case RenderDebugView::SceneDepth: return "Scene Depth";
            case RenderDebugView::SceneColor: return "Scene Color";
            case RenderDebugView::None:
            default: return "None";
            }
        }

        struct DebugViewOption {
            RenderDebugView view = RenderDebugView::None;
            const char* label = "None";
        };

        constexpr DebugViewOption kDebugViewOptions[] = {
            { RenderDebugView::None, "None" },
            { RenderDebugView::Normal, "Normal" },
            { RenderDebugView::Tangent, "Tangent" },
            { RenderDebugView::LightingOnly, "Lighting Only" },
            { RenderDebugView::BaseColor, "Base Color" },
            { RenderDebugView::Roughness, "Roughness" },
            { RenderDebugView::Metallic, "Metallic" },
            { RenderDebugView::Occlusion, "Occlusion" },
            { RenderDebugView::Shadow, "Shadow" },
            { RenderDebugView::NdotL, "NdotL" },
            { RenderDebugView::Emissive, "Emissive" },
            { RenderDebugView::SceneDepth, "Scene Depth" },
            { RenderDebugView::SceneColor, "Scene Color" },
        };

        int DebugViewOptionIndex(RenderDebugView view) {
            for (int i = 0; i < static_cast<int>(std::size(kDebugViewOptions)); ++i) {
                if (kDebugViewOptions[i].view == view) {
                    return i;
                }
            }
            return 0;
        }

        const char* SkyModeName(SkyMode mode) {
            switch (mode) {
            case SkyMode::None: return "None";
            case SkyMode::Cubemap: return "Cubemap";
            case SkyMode::Texture2D: return "Texture2D";
            case SkyMode::Gradient:
            default: return "Gradient";
            }
        }

        const char* ToneMappingModeName(int mode) {
            switch (mode) {
            case 0: return "None";
            case 1: return "Reinhard";
            case 2: return "ACES Approx";
            default: return "Unknown";
            }
        }

        struct SkyPickerEntry {
            std::string id{};
            std::string name{};
            std::string path{};
            std::string state{};
            std::string label{};
        };

        SkyPickerEntry BuildSkyPickerEntry(const AssetDescriptor& descriptor, const AssetDatabase* assetDatabase) {
            SkyPickerEntry entry{};
            entry.id = descriptor.id.value;
            entry.path = descriptor.sourcePath;

            if (assetDatabase) {
                if (const AssetRecord* record = assetDatabase->FindByGuid(AssetGuid{ descriptor.id.value })) {
                    entry.name = record->displayName.empty() ? record->sourcePath.stem().string() : record->displayName;
                    entry.path = record->sourcePath.generic_string();
                    entry.state = ToString(GetImportState(*record));
                }
            }

            if (entry.name.empty()) {
                entry.name = std::filesystem::path(descriptor.sourcePath).stem().string();
            }
            if (entry.name.empty()) {
                entry.name = entry.id.empty() ? "<unnamed sky>" : entry.id;
            }
            if (entry.state.empty()) {
                entry.state = "Registered";
            }

            entry.label = entry.name + "  [" + entry.state + "]##" + entry.id;
            return entry;
        }

        bool DrawSkyAssetPicker(
            const AssetRegistry* assetRegistry,
            const AssetDatabase* assetDatabase,
            std::string& value) {

            if (assetDatabase) {
                return EDITOR::DrawAssetField(
                    assetDatabase,
                    EDITOR::AssetFieldOptions{
                        "Sky Asset",
                        AssetType::Sky,
                        true,
                        false,
                        true
                    },
                    value);
            }

            if (!assetRegistry) {
                char skyAssetBuffer[256]{};
                std::strncpy(skyAssetBuffer, value.c_str(), sizeof(skyAssetBuffer) - 1);
                if (ImGui::InputText("Sky Asset", skyAssetBuffer, sizeof(skyAssetBuffer))) {
                    value = skyAssetBuffer;
                    return true;
                }
                return false;
            }

            std::vector<const AssetDescriptor*> skies = assetRegistry->CollectByType(AssetType::Sky);
            std::vector<SkyPickerEntry> entries;
            entries.reserve(skies.size());
            for (const AssetDescriptor* descriptor : skies) {
                if (descriptor && !descriptor->id.value.empty()) {
                    entries.push_back(BuildSkyPickerEntry(*descriptor, assetDatabase));
                }
            }
            std::sort(entries.begin(), entries.end(), [](const SkyPickerEntry& lhs, const SkyPickerEntry& rhs) {
                return lhs.name < rhs.name;
            });

            const SkyPickerEntry* current = nullptr;
            for (const SkyPickerEntry& entry : entries) {
                if (entry.id == value) {
                    current = &entry;
                    break;
                }
            }

            bool changed = false;
            const std::string preview = value.empty()
                ? std::string("<none>")
                : (current ? current->name : ("Missing: " + value));
            if (ImGui::BeginCombo("Sky Asset", preview.c_str())) {
                if (ImGui::Selectable("<none>", value.empty())) {
                    value.clear();
                    changed = true;
                }
                for (const SkyPickerEntry& entry : entries) {
                    const bool selected = value == entry.id;
                    if (ImGui::Selectable(entry.label.c_str(), selected)) {
                        value = entry.id;
                        changed = true;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("%s\n%s\nGUID: %s",
                            entry.name.c_str(),
                            entry.path.empty() ? "<no source path>" : entry.path.c_str(),
                            entry.id.c_str());
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            if (ImGui::Button("Copy Sky GUID") && !value.empty()) {
                ImGui::SetClipboardText(value.c_str());
            }
            if (!value.empty()) {
                if (current) {
                    ImGui::TextDisabled("%s | %s", current->path.c_str(), current->state.c_str());
                } else {
                    ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.35f, 1.0f), "Unresolved sky asset: %s", value.c_str());
                }
            }
            return changed;
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

    bool EnvironmentPanel::Draw(
        SceneEnvironment& environment,
        const SKYRENDERER::SkyRendererDebugState* skyDebugState,
        const AssetRegistry* assetRegistry,
        const AssetDatabase* assetDatabase) const {
        if (!ImGui::Begin("Environment")) {
            ImGui::End();
            return false;
        }

        // UI 全体の編集前後を比較し、Preset や配列操作もまとめて検出する。
        const SceneEnvironment beforeEdit = environment;

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
            const int presetCount = static_cast<int>(sizeof(presets) / sizeof(presets[0]));
            for (int i = 0; i < presetCount; ++i) {
                ImGui::PushID(i);
                if (ImGui::Button(presets[i])) {
                    ApplyEnvironmentPreset(environment, i);
                }
                if ((i % 3) != 2 && (i + 1) < presetCount) {
                    ImGui::SameLine();
                }
                ImGui::PopID();
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Ambient", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit3("Ambient Color", &environment.ambient.color.x);
            ImGui::DragFloat("Ambient Intensity", &environment.ambient.intensity, 0.01f, 0.0f, 10.0f);
            ImGui::Checkbox("Use Sky Color", &environment.ambient.useSkyColor);
            ImGui::DragFloat("Sky Ambient Blend", &environment.ambient.skyBlend, 0.01f, 0.0f, 1.0f);
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
            int skyMode = static_cast<int>(environment.sky.mode);
            const char* skyModeNames[] = { "None", "Gradient", "Cubemap", "Texture2D" };
            if (ImGui::Combo("Sky Mode", &skyMode, skyModeNames, static_cast<int>(std::size(skyModeNames)))) {
                environment.sky.mode = static_cast<SkyMode>(std::clamp(skyMode, 0, 3));
            }
            DrawSkyAssetPicker(assetRegistry, assetDatabase, environment.sky.skyAsset);
            ImGui::DragFloat("Sky Scale", &environment.sky.scale, 0.01f, 0.0001f, 1000.0f);
            ImGui::DragFloat("Sky Yaw", &environment.sky.yaw, 0.01f);
            ImGui::DragFloat("Sky Exposure", &environment.sky.exposure, 0.01f, 0.0f, 16.0f);
            ImGui::ColorEdit3("Sky Tint", &environment.sky.tint.x);
            ImGui::Checkbox("Follow Camera", &environment.sky.followCamera);

            if (environment.sky.mode == SkyMode::Gradient || environment.sky.mode == SkyMode::Cubemap) {
                if (ImGui::TreeNode("Gradient Fallback")) {
                    ImGui::ColorEdit3("Zenith Color", &environment.sky.zenithColor.x);
                    ImGui::ColorEdit3("Horizon Color", &environment.sky.horizonColor.x);
                    ImGui::ColorEdit3("Ground Color", &environment.sky.groundColor.x);
                    ImGui::DragFloat("Horizon Power", &environment.sky.horizonPower, 0.01f, 0.01f, 8.0f);
                    ImGui::TreePop();
                }
            }
            if (ImGui::TreeNode("Sun Disk")) {
                ImGui::Checkbox("Show Sun Disk", &environment.sky.showSunDisk);
                ImGui::DragFloat("Sun Disk Intensity", &environment.sky.sunDiskIntensity, 0.01f, 0.0f, 32.0f);
                ImGui::DragFloat("Sun Disk Size", &environment.sky.sunDiskSize, 0.001f, 0.001f, 0.5f);
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("Environment Output")) {
                ImGui::DragFloat("Ambient From Sky", &environment.sky.ambientFromSky, 0.01f, 0.0f, 8.0f);
                ImGui::DragFloat("Reflection Intensity", &environment.sky.reflectionIntensity, 0.01f, 0.0f, 8.0f);
                ImGui::Checkbox("Use Sky Color For Ambient", &environment.ambient.useSkyColor);
                ImGui::DragFloat("Sky Ambient Blend", &environment.ambient.skyBlend, 0.01f, 0.0f, 1.0f);
                ImGui::Checkbox("Use Sky Horizon For Fog", &environment.fog.useSkyHorizonColor);
                if (ImGui::Button("Apply Horizon To Fog")) {
                    environment.fog.color = environment.sky.horizonColor;
                }
                ImGui::TreePop();
            }
            if (environment.sky.mode == SkyMode::Cubemap) {
                ImGui::TextColored(ImVec4(0.75f, 0.85f, 1.0f, 1.0f), "Cubemap mode requires DDS cubemap texture.");
                ImGui::TextColored(ImVec4(0.75f, 0.85f, 1.0f, 1.0f), "PNG/JPG panorama should use Texture2D mode.");
            }
            ImGui::Checkbox("Show Sky Debug Texture", &environment.sky.showDebugTexture);
            if (skyDebugState != nullptr) {
                ImGui::Text("Active Sky Asset: %s", skyDebugState->activeSkyAsset.c_str());
                ImGui::Text("Active Texture Path: %s", skyDebugState->activeTexturePath.c_str());
                ImGui::Text("Sky Asset Found: %s", skyDebugState->skyAssetFound ? "true" : "false");
                if (!skyDebugState->skyAssetFound && !skyDebugState->activeSkyAsset.empty()) {
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.45f, 0.35f, 1.0f),
                        "Sky asset is not registered in SkyManager.");
                }
                ImGui::Text("Sky Debug Mode: %s", SkyModeName(skyDebugState->mode));
                ImGui::Text("Cubemap Loaded: %s", skyDebugState->cubemapLoaded ? "true" : "false");
                ImGui::Text("Texture Valid: %s", skyDebugState->textureValid ? "true" : "false");
                ImGui::Text("Cubemap Handle: %d", skyDebugState->cubemapHandle);
                ImGui::Text("Texture Handle: %d", skyDebugState->textureHandle);
                ImGui::Text("Using Fallback: %s", skyDebugState->usingFallback ? "true" : "false");
                ImGui::Text("Sky Draws: %zu", skyDebugState->drawCount);
            }
            if (ImGui::TreeNode("IBL State")) {
                const IBL::IblEnvironmentData& iblData = IBL::GetEnvironmentData();
                ImGui::Text("IBL Valid: %s", iblData.valid ? "Yes" : "No");
                ImGui::Text("Irradiance: %s  Handle: %d", iblData.hasIrradiance ? "Yes" : "No", iblData.irradianceHandle);
                ImGui::Text("Prefiltered: %s  Handle: %d", iblData.hasPrefiltered ? "Yes" : "No", iblData.prefilteredHandle);
                ImGui::Text("BRDF LUT: %s  Handle: %d", iblData.hasBrdfLut ? "Yes" : "No", iblData.brdfLutHandle);
                ImGui::Text("Prefiltered Mips: %u", iblData.prefilteredMipCount);
                ImGui::TextDisabled("No IBL assets are generated automatically yet; missing resources fall back to sky approximation.");
                ImGui::TreePop();
            }
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

        if (ImGui::TreeNode("FXAA")) {
            POST::PostSystem::FxaaSettings fxaa = POST::PostSystem::GetFxaaSettings();
            bool changed = false;
            changed |= ImGui::Checkbox("FXAA Enabled", &fxaa.enabled);
            changed |= ImGui::DragFloat("Edge Threshold", &fxaa.edgeThreshold, 0.001f, 0.0312f, 0.333f, "%.4f");
            changed |= ImGui::DragFloat("Edge Threshold Min", &fxaa.edgeThresholdMin, 0.0005f, 0.0f, 0.0833f, "%.4f");
            changed |= ImGui::DragFloat("Subpixel Quality", &fxaa.subpixelQuality, 0.01f, 0.0f, 1.0f);
            if (changed) {
                POST::PostSystem::SetFxaaSettings(fxaa);
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
            ImGui::Checkbox("Use Sky Horizon Color", &environment.fog.useSkyHorizonColor);
            if (ImGui::Button("Set Fog Color From Sky Horizon")) {
                environment.fog.color = environment.sky.horizonColor;
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Debug")) {
            ImGui::Checkbox("Show Light Debug", &environment.showLightDebug);
            ImGui::Checkbox("Show Point Light Markers", &environment.showPointLightMarkers);
            ImGui::Checkbox("Show Sky Debug Info", &environment.showSkyDebugInfo);

            int debugViewIndex = DebugViewOptionIndex(environment.debugView);
            int selectedDebugViewIndex = debugViewIndex;
            bool debugViewChanged = false;
            const char* debugPreview = kDebugViewOptions[debugViewIndex].label;
            if (ImGui::BeginCombo("Render Debug View", debugPreview)) {
                for (int i = 0; i < static_cast<int>(std::size(kDebugViewOptions)); ++i) {
                    const bool selected = (i == selectedDebugViewIndex);
                    ImGui::PushID(i);
                    if (ImGui::Selectable(kDebugViewOptions[i].label, selected)) {
                        selectedDebugViewIndex = i;
                        debugViewChanged = true;
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            if (debugViewChanged) {
                environment.debugView = kDebugViewOptions[selectedDebugViewIndex].view;
            }

            ImGui::Text("Active Debug View: %s", DebugViewName(environment.debugView));
            if (environment.debugView != RenderDebugView::None) {
                ImGui::TextColored(ImVec4(1.0f, 0.72f, 0.25f, 1.0f), "Debug view overrides the final shaded output.");
            }

            const POST::PostSystem::FxaaSettings fxaaState = POST::PostSystem::GetFxaaSettings();
            ImGui::SeparatorText("Render Quality State");
            ImGui::Text("PBR Mode: Cook-Torrance ON");
            ImGui::Text("DebugView: %s", DebugViewName(environment.debugView));
            ImGui::Text("ToneMapping: %s  Exposure %.2f  Gamma %.2f  Mode %s",
                environment.toneMapping.enabled ? "On" : "Off",
                environment.toneMapping.exposure,
                environment.toneMapping.gamma,
                ToneMappingModeName(environment.toneMapping.mode));
            ImGui::Text("FXAA: %s  Edge %.4f / Min %.4f  Subpixel %.2f",
                fxaaState.enabled ? "On" : "Off",
                fxaaState.edgeThreshold,
                fxaaState.edgeThresholdMin,
                fxaaState.subpixelQuality);
            ImGui::Text("Texture Color Space: Auto / SRGB / Linear enabled");
            const IBL::IblEnvironmentData& iblData = IBL::GetEnvironmentData();
            ImGui::Text("IBL: %s  Irr %s  Pref %s  BRDF %s  Mips %u",
                iblData.valid ? "Ready" : "Fallback",
                iblData.hasIrradiance ? "Yes" : "No",
                iblData.hasPrefiltered ? "Yes" : "No",
                iblData.hasBrdfLut ? "Yes" : "No",
                iblData.prefilteredMipCount);

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
                ImGui::Text("Mode: %s", SkyModeName(skyDebugState->mode));
                ImGui::Text("Cubemap Loaded: %s", skyDebugState->cubemapLoaded ? "true" : "false");
                ImGui::Text("Using Fallback: %s", skyDebugState->usingFallback ? "true" : "false");
                ImGui::Text("Texture Valid: %s", skyDebugState->textureValid ? "true" : "false");
                ImGui::Text("Texture Handle / Cubemap Handle: %d / %d", skyDebugState->textureHandle, skyDebugState->cubemapHandle);
                ImGui::Text("PSO Creates / Draws: %zu / %zu", skyDebugState->psoCreateCount, skyDebugState->drawCount);
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

        const bool changed = !EqualSceneEnvironment(beforeEdit, environment);
        ImGui::End();
        return changed;
    }
#else
    bool EnvironmentPanel::Draw(SceneEnvironment&, const SKYRENDERER::SkyRendererDebugState*, const AssetRegistry*, const AssetDatabase*) const { return false; }
#endif

} // namespace HIKARI
