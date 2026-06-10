#include "HIKARI_EnvironmentPanel.h"
#include "Assets/HIKARI_AssetDatabase.h"
#include "Assets/HIKARI_AssetImportState.h"
#include "Assets/HIKARI_AssetRegistry.h"
#include "Assets/HIKARI_AssetTypes.h"
#include "Editor/Widgets/HIKARI_AssetFieldWidget.h"
#include "Render3D/Diagnostics/HIKARI_EnvironmentDiagnostics.h"
#include "Render3D/HIKARI_Math3D.h"
#include "Render3D/Lighting/HIKARI_SceneEnvironment.h"
#include "Render3D/Lighting/HIKARI_SkyRenderer.h"
#include "Vfx/Post/HIKARI_PostProfile.h"
#include "Vfx/Post/HIKARI_PostSystem.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneTransitionBus.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <utility>
#include <vector>
#endif

namespace HIKARI {

#if defined(HIKARI_WITH_EDITOR)
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

        bool EqualReflectionProbeSettings(const ReflectionProbeSettings& lhs, const ReflectionProbeSettings& rhs) {
            return lhs.enabled == rhs.enabled &&
                lhs.sourceCubemapAsset == rhs.sourceCubemapAsset &&
                EqualVec3(lhs.position, rhs.position) &&
                lhs.radius == rhs.radius &&
                lhs.intensity == rhs.intensity &&
                lhs.influenceShape == rhs.influenceShape &&
                EqualVec3(lhs.influenceBoxCenter, rhs.influenceBoxCenter) &&
                EqualVec3(lhs.influenceBoxSize, rhs.influenceBoxSize) &&
                lhs.projectionShape == rhs.projectionShape &&
                EqualVec3(lhs.projectionBoxCenter, rhs.projectionBoxCenter) &&
                EqualVec3(lhs.projectionBoxSize, rhs.projectionBoxSize) &&
                lhs.blendDistance == rhs.blendDistance &&
                lhs.priority == rhs.priority;
        }

        MATH::Vec3 ReflectionProbeRadiusBoxSize(float radius) {
            const float diameter = (std::max)(0.01f, radius * 2.0f);
            return { diameter, diameter, diameter };
        }

        void ClampReflectionProbeBoxSize(MATH::Vec3& size) {
            size.x = (std::max)(0.001f, size.x);
            size.y = (std::max)(0.001f, size.y);
            size.z = (std::max)(0.001f, size.z);
        }

        void FitReflectionProbeBoxFromRadius(MATH::Vec3& center, MATH::Vec3& size, const ReflectionProbeSettings& probe) {
            center = probe.position;
            size = ReflectionProbeRadiusBoxSize(probe.radius);
        }

        const char* ReflectionProbeInfluenceShapeName(ReflectionProbeInfluenceShape shape) {
            return shape == ReflectionProbeInfluenceShape::Box ? "Box" : "Sphere";
        }

        const char* ReflectionProbeProjectionShapeName(ReflectionProbeProjectionShape shape) {
            return shape == ReflectionProbeProjectionShape::Box ? "Box" : "Infinite";
        }

        bool DrawReflectionProbeInfluenceShapeCombo(ReflectionProbeInfluenceShape& shape) {
            const char* names[] = { "Sphere", "Box" };
            int index = shape == ReflectionProbeInfluenceShape::Box ? 1 : 0;
            if (!ImGui::Combo("Influence Shape", &index, names, static_cast<int>(std::size(names)))) {
                return false;
            }
            shape = index == 1 ? ReflectionProbeInfluenceShape::Box : ReflectionProbeInfluenceShape::Sphere;
            return true;
        }

        bool DrawReflectionProbeProjectionShapeCombo(ReflectionProbeProjectionShape& shape) {
            const char* names[] = { "Infinite", "Box" };
            int index = shape == ReflectionProbeProjectionShape::Box ? 1 : 0;
            if (!ImGui::Combo("Projection Shape", &index, names, static_cast<int>(std::size(names)))) {
                return false;
            }
            shape = index == 1 ? ReflectionProbeProjectionShape::Box : ReflectionProbeProjectionShape::Infinite;
            return true;
        }

        int SsaoModeIndex(const AmbientOcclusionSettings& settings) {
            if (!settings.enabled || settings.mode == SsaoMode::Off) {
                return 0;
            }
            switch (settings.mode) {
            case SsaoMode::Reference: return 1;
            case SsaoMode::OptimizedHigh: return 2;
            case SsaoMode::Balanced: return 3;
            case SsaoMode::Off:
            default: return 0;
            }
        }

        SsaoMode SsaoModeFromIndex(int index) {
            switch (index) {
            case 1: return SsaoMode::Reference;
            case 2: return SsaoMode::OptimizedHigh;
            case 3: return SsaoMode::Balanced;
            case 0:
            default: return SsaoMode::Off;
            }
        }

        bool EqualAmbientOcclusionSettings(const AmbientOcclusionSettings& lhs, const AmbientOcclusionSettings& rhs) {
            return lhs.enabled == rhs.enabled &&
                lhs.mode == rhs.mode &&
                lhs.radius == rhs.radius &&
                lhs.bias == rhs.bias &&
                lhs.strength == rhs.strength &&
                lhs.power == rhs.power &&
                lhs.diffuseStrength == rhs.diffuseStrength &&
                lhs.specularStrength == rhs.specularStrength &&
                lhs.sampleCount == rhs.sampleCount &&
                lhs.blurIterations == rhs.blurIterations;
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
                !EqualReflectionProbeSettings(lhs.reflectionProbe, rhs.reflectionProbe) ||
                !EqualAmbientOcclusionSettings(lhs.ambientOcclusion, rhs.ambientOcclusion) ||
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

        bool DrawReflectionProbeAssetPicker(
            const AssetRegistry* assetRegistry,
            const AssetDatabase* assetDatabase,
            std::string& value) {

            if (assetDatabase) {
                return EDITOR::DrawAssetField(
                    assetDatabase,
                    EDITOR::AssetFieldOptions{
                        "Source Cubemap Asset",
                        AssetType::Sky,
                        true,
                        false,
                        true
                    },
                    value);
            }

            if (!assetRegistry) {
                char assetBuffer[256]{};
                std::strncpy(assetBuffer, value.c_str(), sizeof(assetBuffer) - 1);
                if (ImGui::InputText("Source Cubemap Asset", assetBuffer, sizeof(assetBuffer))) {
                    value = assetBuffer;
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
            if (ImGui::BeginCombo("Source Cubemap Asset", preview.c_str())) {
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
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
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
        const SKYRENDERER::SkyRendererDebugState*,
        const AssetRegistry* assetRegistry,
        const AssetDatabase* assetDatabase) const {
        if (!ImGui::Begin("Environment")) {
            ImGui::End();
            return false;
        }

        // UI 蜈ｨ菴薙・邱ｨ髮・燕蠕後ｒ豈碑ｼ・＠縲￣reset 繧・・蛻玲桃菴懊ｂ縺ｾ縺ｨ繧√※讀懷・縺吶ｋ縲・
        // 邱ｨ髮・､縺ｯ蜊ｳ譎ゅ↓ runtime 縺ｸ蜿肴丐縺励∬ｩｳ邏ｰ險ｺ譁ｭ縺ｯ log 縺ｸ騾・′縺吶・
        const SceneEnvironment beforeEdit = environment;

        ImGui::SeparatorText("Scene Environment");
        const RENDER3D::DIAGNOSTICS::EnvironmentDiagnosticsSnapshot runtimeSnapshot =
            RENDER3D::DIAGNOSTICS::CaptureEnvironmentSnapshot(&environment);
        ImGui::TextDisabled("Runtime: %s | %s | Errors %u",
            RENDER3D::DIAGNOSTICS::ResolveSkySummaryLabel(runtimeSnapshot),
            RENDER3D::DIAGNOSTICS::ResolveIblSummaryLabel(runtimeSnapshot),
            runtimeSnapshot.recentRenderErrorCount);
        if (runtimeSnapshot.recentRenderErrorCount > 0) {
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "Check log");
        }

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
            float acneFix = (std::max)(environment.directionalShadow.depthBias * 1000.0f, environment.directionalShadow.normalBias * 25.0f);
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
            ImGui::TextDisabled("Runtime Sky: %s", RENDER3D::DIAGNOSTICS::ResolveSkySummaryLabel(runtimeSnapshot));
            ImGui::TextDisabled("Runtime IBL: %s", RENDER3D::DIAGNOSTICS::ResolveIblSummaryLabel(runtimeSnapshot));
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Reflection Probe")) {
            ReflectionProbeSettings& probe = environment.reflectionProbe;
            ImGui::Checkbox("Enabled", &probe.enabled);
            DrawReflectionProbeAssetPicker(assetRegistry, assetDatabase, probe.sourceCubemapAsset);
            ImGui::DragFloat3("Position", &probe.position.x, 0.02f);
            ImGui::DragFloat("Radius", &probe.radius, 0.05f, 0.01f, 500.0f);
            probe.radius = (std::max)(0.01f, probe.radius);
            ImGui::DragFloat("Intensity", &probe.intensity, 0.01f, 0.0f, 8.0f);
            probe.intensity = (std::max)(0.0f, probe.intensity);

            const ReflectionProbeInfluenceShape previousInfluenceShape = probe.influenceShape;
            if (DrawReflectionProbeInfluenceShapeCombo(probe.influenceShape) &&
                previousInfluenceShape != ReflectionProbeInfluenceShape::Box &&
                probe.influenceShape == ReflectionProbeInfluenceShape::Box) {
                // Shape 螟画峩譎ゅ・ sphere 險ｭ螳壹°繧・box 蛻晄悄蛟､繧剃ｽ懊ｋ縲・
                FitReflectionProbeBoxFromRadius(probe.influenceBoxCenter, probe.influenceBoxSize, probe);
            }
            if (probe.influenceShape == ReflectionProbeInfluenceShape::Box) {
                ImGui::DragFloat3("Influence Box Center", &probe.influenceBoxCenter.x, 0.02f);
                ImGui::DragFloat3("Influence Box Size", &probe.influenceBoxSize.x, 0.02f, 0.001f, 1000.0f);
                ClampReflectionProbeBoxSize(probe.influenceBoxSize);
            } else {
                ImGui::TextDisabled("Influence Shape: %s radius %.2f",
                    ReflectionProbeInfluenceShapeName(probe.influenceShape),
                    probe.radius);
            }

            const ReflectionProbeProjectionShape previousProjectionShape = probe.projectionShape;
            if (DrawReflectionProbeProjectionShapeCombo(probe.projectionShape) &&
                previousProjectionShape != ReflectionProbeProjectionShape::Box &&
                probe.projectionShape == ReflectionProbeProjectionShape::Box) {
                // Projection proxy 繧・capture 菴咲ｽｮ縺九ｉ蛻晄悄蛹悶☆繧九・
                FitReflectionProbeBoxFromRadius(probe.projectionBoxCenter, probe.projectionBoxSize, probe);
            }
            if (probe.projectionShape == ReflectionProbeProjectionShape::Box) {
                ImGui::DragFloat3("Projection Box Center", &probe.projectionBoxCenter.x, 0.02f);
                ImGui::DragFloat3("Projection Box Size", &probe.projectionBoxSize.x, 0.02f, 0.001f, 1000.0f);
                ClampReflectionProbeBoxSize(probe.projectionBoxSize);
            } else {
                ImGui::TextDisabled("Projection Shape: %s",
                    ReflectionProbeProjectionShapeName(probe.projectionShape));
            }

            ImGui::DragFloat("Blend Distance", &probe.blendDistance, 0.02f, 0.0f, 500.0f);
            probe.blendDistance = (std::max)(0.0f, probe.blendDistance);
            ImGui::DragInt("Priority", &probe.priority, 1.0f, -1000, 1000);

            if (ImGui::Button("Copy Position To Boxes")) {
                probe.influenceBoxCenter = probe.position;
                probe.projectionBoxCenter = probe.position;
            }
            ImGui::SameLine();
            if (ImGui::Button("Fit Boxes From Radius")) {
                probe.influenceBoxSize = ReflectionProbeRadiusBoxSize(probe.radius);
                probe.projectionBoxSize = probe.influenceBoxSize;
            }
            ImGui::TextDisabled("Runtime Probe: %s",
                RENDER3D::DIAGNOSTICS::ResolveReflectionProbeSummaryLabel(runtimeSnapshot));
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Ambient Occlusion")) {
            int ssaoModeIndex = SsaoModeIndex(environment.ambientOcclusion);
            const char* ssaoModeNames[] = { "Off", "Reference", "OptimizedHigh", "Balanced" };
            if (ImGui::Combo("SSAO Mode", &ssaoModeIndex, ssaoModeNames, static_cast<int>(std::size(ssaoModeNames)))) {
                environment.ambientOcclusion.mode = SsaoModeFromIndex(ssaoModeIndex);
                environment.ambientOcclusion.enabled = environment.ambientOcclusion.mode != SsaoMode::Off;
            }
            ImGui::DragFloat("Radius", &environment.ambientOcclusion.radius, 0.01f, 0.01f, 10.0f);
            ImGui::DragFloat("Bias", &environment.ambientOcclusion.bias, 0.001f, 0.0f, 0.5f, "%.4f");
            ImGui::DragFloat("Strength", &environment.ambientOcclusion.strength, 0.01f, 0.0f, 4.0f);
            ImGui::DragFloat("Power", &environment.ambientOcclusion.power, 0.01f, 0.1f, 8.0f);
            ImGui::DragFloat("Diffuse Strength", &environment.ambientOcclusion.diffuseStrength, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Specular Strength", &environment.ambientOcclusion.specularStrength, 0.01f, 0.0f, 1.0f);
            int sampleIndex = 0;
            const uint32_t samples = environment.ambientOcclusion.sampleCount;
            if (samples <= 8u) sampleIndex = 0;
            else if (samples <= 16u) sampleIndex = 1;
            else if (samples <= 24u) sampleIndex = 2;
            else sampleIndex = 3;
            const char* sampleNames[] = { "8", "16", "24", "32" };
            if (ImGui::Combo("Samples", &sampleIndex, sampleNames, static_cast<int>(std::size(sampleNames)))) {
                const uint32_t sampleValues[] = { 8u, 16u, 24u, 32u };
                environment.ambientOcclusion.sampleCount = sampleValues[std::clamp(sampleIndex, 0, 3)];
            }
            int blurIterations = static_cast<int>(environment.ambientOcclusion.blurIterations);
            if (ImGui::SliderInt("Blur Iterations", &blurIterations, 0, 4)) {
                environment.ambientOcclusion.blurIterations = static_cast<uint32_t>(std::clamp(blurIterations, 0, 4));
            }
            if (environment.ambientOcclusion.mode == SsaoMode::OptimizedHigh) {
                ImGui::TextDisabled("Runtime: OptimizedHigh uses half-res AO and depth-aware upsample; samples 16-24, blur <= 2.");
            } else if (environment.ambientOcclusion.mode == SsaoMode::Balanced) {
                ImGui::TextDisabled("Runtime: Balanced uses half-res AO and depth-aware upsample; samples <= 16, blur <= 1.");
            }
            ImGui::TextDisabled("Runtime AO: %s",
                RENDER3D::DIAGNOSTICS::ResolveSsaoSummaryLabel(runtimeSnapshot));
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

            ImGui::TextDisabled("Runtime: %s | %s | Errors %u",
                RENDER3D::DIAGNOSTICS::ResolveSkySummaryLabel(runtimeSnapshot),
                RENDER3D::DIAGNOSTICS::ResolveIblSummaryLabel(runtimeSnapshot),
                runtimeSnapshot.recentRenderErrorCount);
            if (runtimeSnapshot.recentRenderErrorCount > 0) {
                ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.35f, 1.0f), "Render diagnostics contain recent errors.");
            }

            // Debug dump 縺ｯ scene dirty 縺ｫ縺励↑縺・・
            if (ImGui::Button("Dump Environment Diagnostics")) {
                RENDER3D::DIAGNOSTICS::LogEnvironmentSnapshot("EnvironmentPanel", &environment);
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
