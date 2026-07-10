#include "HIKARI_QualityPanel.h"

#include "HIKARI_Services.h"
#include "Render3D/Diagnostics/HIKARI_EnvironmentDiagnostics.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Scene/HIKARI_RuntimeSceneContext.h"
#include "Scene/HIKARI_SceneTransitionBus.h"
#include "Vfx/Post/HIKARI_PostProfile.h"
#include "Vfx/Post/HIKARI_PostSystem.h"

#if defined(HIKARI_WITH_EDITOR)
#include "imgui.h"
#include <algorithm>
#include <cstring>
#include <iterator>
#include <string>
#include <utility>
#endif

namespace HIKARI {

#if defined(HIKARI_WITH_EDITOR)
    namespace {
        constexpr const char* kDefaultGlobalPostProfileId = "Ani";

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
                if (param.ref.channel <= 2) {
                    changed = ImGui::DragFloat2(label, &value[param.ref.channel], param.speed, param.minValues[0], param.maxValues[0]);
                }
                break;
            case VFX::ParamType::Float3:
                if (param.ref.channel <= 1) {
                    changed = ImGui::DragFloat3(label, &value[param.ref.channel], param.speed, param.minValues[0], param.maxValues[0]);
                }
                break;
            case VFX::ParamType::Float4:
                if (param.ref.channel == 0) {
                    changed = ImGui::DragFloat4(label, &value[param.ref.channel], param.speed, param.minValues[0], param.maxValues[0]);
                }
                break;
            case VFX::ParamType::Color:
                if (param.ref.channel == 0) {
                    changed = ImGui::ColorEdit4(label, &value[param.ref.channel]);
                }
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

        bool EqualFloat4(const DirectX::XMFLOAT4& lhs, const DirectX::XMFLOAT4& rhs) {
            return lhs.x == rhs.x && lhs.y == rhs.y && lhs.z == rhs.z && lhs.w == rhs.w;
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

        bool EqualAmbientOcclusionSettings(
            const AmbientOcclusionSettings& lhs,
            const AmbientOcclusionSettings& rhs) {
            return lhs.enabled == rhs.enabled &&
                lhs.mode == rhs.mode &&
                lhs.radius == rhs.radius &&
                lhs.bias == rhs.bias &&
                lhs.strength == rhs.strength &&
                lhs.power == rhs.power &&
                lhs.diffuseStrength == rhs.diffuseStrength &&
                lhs.specularStrength == rhs.specularStrength &&
                lhs.sampleCount == rhs.sampleCount &&
                lhs.blurIterations == rhs.blurIterations &&
                lhs.editorViewportSuppressed == rhs.editorViewportSuppressed;
        }

        bool EqualDirectionalShadowSettings(
            const DirectionalShadowSettings& lhs,
            const DirectionalShadowSettings& rhs) {
            return lhs.enabled == rhs.enabled &&
                lhs.resolution == rhs.resolution &&
                lhs.orthoSize == rhs.orthoSize &&
                lhs.nearPlane == rhs.nearPlane &&
                lhs.farPlane == rhs.farPlane &&
                lhs.depthBias == rhs.depthBias &&
                lhs.normalBias == rhs.normalBias &&
                lhs.strength == rhs.strength &&
                lhs.pcfEnabled == rhs.pcfEnabled &&
                lhs.pcfRadius == rhs.pcfRadius &&
                lhs.stabilize == rhs.stabilize &&
                lhs.showDebugTexture == rhs.showDebugTexture &&
                lhs.shadowDistance == rhs.shadowDistance &&
                lhs.showDebugFrustum == rhs.showDebugFrustum;
        }

        bool EqualQualityEnvironment(const SceneEnvironment& lhs, const SceneEnvironment& rhs) {
            return EqualDirectionalShadowSettings(lhs.directionalShadow, rhs.directionalShadow) &&
                EqualAmbientOcclusionSettings(lhs.ambientOcclusion, rhs.ambientOcclusion) &&
                lhs.bloom.enabled == rhs.bloom.enabled &&
                lhs.bloom.threshold == rhs.bloom.threshold &&
                lhs.bloom.intensity == rhs.bloom.intensity &&
                lhs.bloom.radius == rhs.bloom.radius &&
                lhs.bloom.downsampleCount == rhs.bloom.downsampleCount &&
                lhs.toneMapping.enabled == rhs.toneMapping.enabled &&
                lhs.toneMapping.exposure == rhs.toneMapping.exposure &&
                lhs.toneMapping.gamma == rhs.toneMapping.gamma &&
                lhs.toneMapping.mode == rhs.toneMapping.mode &&
                EqualPostSettings(lhs.post, rhs.post);
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

        bool DrawResolutionPresetCombo(
            const char* label,
            RENDER3D::RenderResolutionPreset& preset,
            bool includeViewport) {
            bool changed = false;
            if (ImGui::BeginCombo(label, RENDER3D::RenderResolutionPresetLabel(preset))) {
                const RENDER3D::RenderResolutionPreset presets[] = {
                    RENDER3D::RenderResolutionPreset::Viewport,
                    RENDER3D::RenderResolutionPreset::P720,
                    RENDER3D::RenderResolutionPreset::P1080,
                    RENDER3D::RenderResolutionPreset::P1440,
                };
                for (RENDER3D::RenderResolutionPreset candidate : presets) {
                    if (!includeViewport && candidate == RENDER3D::RenderResolutionPreset::Viewport) {
                        continue;
                    }
                    const bool selected = preset == candidate;
                    if (ImGui::Selectable(RENDER3D::RenderResolutionPresetLabel(candidate), selected)) {
                        preset = candidate;
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

        bool DrawWindowModeCombo(RENDER3D::WindowPresentationMode& mode) {
            bool changed = false;
            if (ImGui::BeginCombo("Window Mode", RENDER3D::WindowPresentationModeLabel(mode))) {
                const RENDER3D::WindowPresentationMode modes[] = {
                    RENDER3D::WindowPresentationMode::Windowed,
                    RENDER3D::WindowPresentationMode::BorderlessWindow,
                    RENDER3D::WindowPresentationMode::Fullscreen,
                };
                for (RENDER3D::WindowPresentationMode candidate : modes) {
                    const bool selected = mode == candidate;
                    if (ImGui::Selectable(RENDER3D::WindowPresentationModeLabel(candidate), selected)) {
                        mode = candidate;
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

        bool DrawGeometryPipelineCombo(RENDER3D::GeometryPipelineMode& mode) {
            bool changed = false;
            if (ImGui::BeginCombo("Geometry Backend", RENDER3D::GeometryPipelineModeLabel(mode))) {
                const RENDER3D::GeometryPipelineMode modes[] = {
                    RENDER3D::GeometryPipelineMode::MeshShader,
                    RENDER3D::GeometryPipelineMode::TraditionalVsPs,
                    RENDER3D::GeometryPipelineMode::AutoFallback,
                };
                for (RENDER3D::GeometryPipelineMode candidate : modes) {
                    const bool selected = mode == candidate;
                    if (ImGui::Selectable(RENDER3D::GeometryPipelineModeLabel(candidate), selected)) {
                        mode = candidate;
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

        bool DrawForwardCostModeCombo(RENDER3D::ForwardShadingCostMode& mode) {
            bool changed = false;
            if (ImGui::BeginCombo("Forward Cost Mode", RENDER3D::ForwardShadingCostModeLabel(mode))) {
                const RENDER3D::ForwardShadingCostMode modes[] = {
                    RENDER3D::ForwardShadingCostMode::Full,
                    RENDER3D::ForwardShadingCostMode::AlbedoOnly,
                    RENDER3D::ForwardShadingCostMode::NoNormalMap,
                    RENDER3D::ForwardShadingCostMode::NoShadow,
                    RENDER3D::ForwardShadingCostMode::NoSsao,
                    RENDER3D::ForwardShadingCostMode::NoMaterialExtras,
                };
                for (RENDER3D::ForwardShadingCostMode candidate : modes) {
                    const bool selected = mode == candidate;
                    if (ImGui::Selectable(RENDER3D::ForwardShadingCostModeLabel(candidate), selected)) {
                        mode = candidate;
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

        bool DrawLightProbeVolumeSamplingCombo(RENDER3D::LightProbeVolumeSamplingMode& mode) {
            bool changed = false;
            if (ImGui::BeginCombo("Light Probe Sampling", RENDER3D::LightProbeVolumeSamplingModeLabel(mode))) {
                const RENDER3D::LightProbeVolumeSamplingMode modes[] = {
                    RENDER3D::LightProbeVolumeSamplingMode::FastSmooth,
                    RENDER3D::LightProbeVolumeSamplingMode::FullTrilinear,
                    RENDER3D::LightProbeVolumeSamplingMode::Off,
                };
                for (RENDER3D::LightProbeVolumeSamplingMode candidate : modes) {
                    const bool selected = mode == candidate;
                    if (ImGui::Selectable(RENDER3D::LightProbeVolumeSamplingModeLabel(candidate), selected)) {
                        mode = candidate;
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

        bool DrawAntiAliasingModeCombo(RENDER3D::RenderAntiAliasingMode& mode) {
            bool changed = false;
            if (ImGui::BeginCombo("Anti-Aliasing", RENDER3D::RenderAntiAliasingModeLabel(mode))) {
                const RENDER3D::RenderAntiAliasingMode modes[] = {
                    RENDER3D::RenderAntiAliasingMode::Off,
                    RENDER3D::RenderAntiAliasingMode::FXAA,
                    RENDER3D::RenderAntiAliasingMode::TAA,
                    RENDER3D::RenderAntiAliasingMode::DLSS,
                };
                for (RENDER3D::RenderAntiAliasingMode candidate : modes) {
                    const bool selected = mode == candidate;
                    if (ImGui::Selectable(RENDER3D::RenderAntiAliasingModeLabel(candidate), selected)) {
                        mode = candidate;
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

        bool DrawFxaaSettings() {
            POST::PostSystem::FxaaSettings fxaa = POST::PostSystem::GetFxaaSettings();
            bool changed = false;
            changed |= ImGui::DragFloat("FXAA Edge Threshold", &fxaa.edgeThreshold, 0.001f, 0.0312f, 0.333f, "%.4f");
            changed |= ImGui::DragFloat("FXAA Edge Threshold Min", &fxaa.edgeThresholdMin, 0.0005f, 0.0f, 0.0833f, "%.4f");
            changed |= ImGui::DragFloat("FXAA Subpixel Quality", &fxaa.subpixelQuality, 0.01f, 0.0f, 1.0f);
            if (changed) {
                POST::PostSystem::SetFxaaSettings(fxaa);
            }
            return changed;
        }

        void DrawRenderSettings() {
            RENDER3D::RenderQualitySettings settings =
                RENDER3D::GetRenderQualitySettings();
            bool changed = false;

            if (ImGui::TreeNodeEx("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= DrawResolutionPresetCombo("Render Resolution", settings.sceneResolution, true);
                if (settings.sceneResolution == RENDER3D::RenderResolutionPreset::Viewport) {
                    changed |= ImGui::SliderFloat("Viewport Scale", &settings.viewportScale, 0.25f, 2.0f, "%.2fx");
                }
                changed |= DrawResolutionPresetCombo("Window Size", settings.windowSize, false);
                changed |= DrawWindowModeCombo(settings.windowMode);
                changed |= ImGui::Checkbox("VSync", &settings.vSync);
                int captureWidth = 0;
                int captureHeight = 0;
                POST::PostSystem::GetSceneCaptureSize(captureWidth, captureHeight);
                ImGui::TextDisabled("Internal Render: %d x %d", captureWidth, captureHeight);
                if (ImGui::Button("Apply Window")) {
                    RENDER3D::SetRenderQualitySettings(settings);
                    SERVICES::ApplyWindowPresentationSettings();
                    changed = false;
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Pipeline", ImGuiTreeNodeFlags_DefaultOpen)) {
                changed |= DrawGeometryPipelineCombo(settings.geometryPipeline);
                changed |= DrawForwardCostModeCombo(settings.forwardCostMode);
                changed |= DrawLightProbeVolumeSamplingCombo(settings.lightProbeVolumeSampling);
                changed |= DrawAntiAliasingModeCombo(settings.antiAliasingMode);
                if (settings.antiAliasingMode == RENDER3D::RenderAntiAliasingMode::TAA) {
                    changed |= ImGui::SliderFloat(
                        "TAA History",
                        &settings.taaHistoryWeight,
                        0.0f,
                        0.97f,
                        "%.2f");
                    changed |= ImGui::SliderFloat(
                        "TAA Variance Clip",
                        &settings.taaVarianceClipGamma,
                        0.0f,
                        3.0f,
                        "%.2f");
                    changed |= ImGui::DragFloat(
                        "TAA Depth Reject",
                        &settings.taaDepthRejection,
                        0.0005f,
                        0.0001f,
                        0.05f,
                        "%.4f");
                    changed |= ImGui::SliderFloat(
                        "TAA Luma Reject",
                        &settings.taaLuminanceRejection,
                        0.05f,
                        4.0f,
                        "%.2f");
                    changed |= ImGui::SliderFloat(
                        "TAA Sharpness",
                        &settings.taaSharpness,
                        0.0f,
                        1.0f,
                        "%.2f");
                } else if (settings.antiAliasingMode == RENDER3D::RenderAntiAliasingMode::FXAA) {
                    (void)DrawFxaaSettings();
                }
                changed |= ImGui::Checkbox("Scene Depth Prepass", &settings.sceneDepthPrepass);
                ImGui::TreePop();
            }

            if (changed) {
                RENDER3D::SetRenderQualitySettings(settings);
            }
        }

        void DrawDirectionalShadow(SceneEnvironment& environment) {
            ImGui::Checkbox("Shadow Enabled", &environment.directionalShadow.enabled);
            int shadowQuality = environment.directionalShadow.resolution <= 1024
                ? 0
                : (environment.directionalShadow.resolution >= 4096 ? 2 : 1);
            const char* qualityNames[] = { "Low", "Medium", "High" };
            if (ImGui::Combo("Shadow Quality", &shadowQuality, qualityNames, static_cast<int>(std::size(qualityNames)))) {
                environment.directionalShadow.resolution =
                    shadowQuality == 0 ? 1024u : (shadowQuality == 2 ? 4096u : 2048u);
            }
            ImGui::DragFloat("Shadow Softness", &environment.directionalShadow.pcfRadius, 0.05f, 0.0f, 4.0f);
            ImGui::DragFloat("Shadow Range", &environment.directionalShadow.orthoSize, 0.1f, 1.0f, 200.0f);
            float acneFix =
                (std::max)(environment.directionalShadow.depthBias * 1000.0f,
                    environment.directionalShadow.normalBias * 25.0f);
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
        }

        void DrawAmbientOcclusion(
            SceneEnvironment& environment,
            const RENDER3D::DIAGNOSTICS::EnvironmentDiagnosticsSnapshot& runtimeSnapshot) {
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
                environment.ambientOcclusion.sampleCount =
                    sampleValues[std::clamp(sampleIndex, 0, 3)];
            }
            int blurIterations = static_cast<int>(environment.ambientOcclusion.blurIterations);
            if (ImGui::SliderInt("Blur Iterations", &blurIterations, 0, 4)) {
                environment.ambientOcclusion.blurIterations =
                    static_cast<uint32_t>(std::clamp(blurIterations, 0, 4));
            }
            ImGui::TextDisabled("Runtime AO: %s",
                RENDER3D::DIAGNOSTICS::ResolveSsaoSummaryLabel(runtimeSnapshot));
        }

        void DrawBloom(SceneEnvironment& environment) {
            ImGui::Checkbox("Bloom Enabled", &environment.bloom.enabled);
            ImGui::DragFloat("Threshold", &environment.bloom.threshold, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Intensity", &environment.bloom.intensity, 0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Radius", &environment.bloom.radius, 0.01f, 0.0f, 8.0f);
            int downsampleCount = static_cast<int>(environment.bloom.downsampleCount);
            if (ImGui::SliderInt("Downsample Count", &downsampleCount, 1, 5)) {
                environment.bloom.downsampleCount =
                    static_cast<uint32_t>(std::clamp(downsampleCount, 1, 5));
            }
        }

        void DrawToneMapping(SceneEnvironment& environment) {
            ImGui::Checkbox("Tone Mapping Enabled", &environment.toneMapping.enabled);
            ImGui::DragFloat("Exposure", &environment.toneMapping.exposure, 0.01f, 0.0f, 8.0f);
            ImGui::DragFloat("Gamma", &environment.toneMapping.gamma, 0.01f, 0.1f, 4.0f);
            const char* modes[] = { "None", "Reinhard", "ACES Approx" };
            int mode = std::clamp(environment.toneMapping.mode, 0, 2);
            if (ImGui::Combo("Mode", &mode, modes, static_cast<int>(std::size(modes)))) {
                environment.toneMapping.mode = mode;
            }
        }

        void DrawGlobalPost(SceneEnvironment& environment) {
            const bool wasPostEnabled = environment.post.enabled;
            if (ImGui::Checkbox("Post Enabled", &environment.post.enabled) &&
                environment.post.enabled &&
                !wasPostEnabled &&
                environment.post.globalPostProfileId.empty()) {
                environment.post.globalPostProfileId = kDefaultGlobalPostProfileId;
                environment.post.valuesInitialized = false;
            }

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
                    const bool profileChanged = environment.post.globalPostProfileId != previousProfileId;
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
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.5f, 0.5f, 1.0f),
                        "Profile not found: %s",
                        environment.post.globalPostProfileId.c_str());
                }
            }

            if (ImGui::TreeNode("Advanced Raw Parameter Block")) {
                for (int i = 0; i < 16; ++i) {
                    ImGui::PushID(i);
                    ImGui::InputFloat4("Param", &environment.post.paramValues[i].x);
                    ImGui::PopID();
                }
                ImGui::TreePop();
            }
        }

        void DrawTransitionDebug() {
            const SceneTransitionBus* transitionBus = RuntimeSceneContext::GetTransitionBus();
            if (!transitionBus) {
                ImGui::TextDisabled("Transition bus unavailable.");
                return;
            }

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
    }

    bool QualityPanel::Draw(SceneEnvironment& environment) const {
        if (!ImGui::Begin("Quality")) {
            ImGui::End();
            return false;
        }

        const SceneEnvironment beforeEdit = environment;
        const RENDER3D::DIAGNOSTICS::EnvironmentDiagnosticsSnapshot runtimeSnapshot =
            RENDER3D::DIAGNOSTICS::CaptureEnvironmentSnapshot(&environment);

        DrawRenderSettings();

        if (ImGui::TreeNodeEx("Directional Shadow", ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawDirectionalShadow(environment);
            ImGui::TreePop();
        }
        if (ImGui::TreeNodeEx("Ambient Occlusion", ImGuiTreeNodeFlags_DefaultOpen)) {
            DrawAmbientOcclusion(environment, runtimeSnapshot);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Bloom")) {
            DrawBloom(environment);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Tone Mapping")) {
            DrawToneMapping(environment);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Global Post")) {
            DrawGlobalPost(environment);
            ImGui::TreePop();
        }
        if (ImGui::TreeNode("Transition Debug")) {
            DrawTransitionDebug();
            ImGui::TreePop();
        }

        ImGui::End();
        return !EqualQualityEnvironment(beforeEdit, environment);
    }
#else
    bool QualityPanel::Draw(SceneEnvironment&) const { return false; }
#endif

} // namespace HIKARI
