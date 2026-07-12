#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"

#include <algorithm>

#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"

namespace HIKARI::RENDER3D {

    namespace {
        RenderQualitySettings gRenderQualitySettings{};

        RenderResolution ClampResolution(RenderResolution resolution) {
            resolution.width = std::clamp(resolution.width, 16, 8192);
            resolution.height = std::clamp(resolution.height, 16, 8192);
            return resolution;
        }
    }

    RenderQualitySettings& GetRenderQualitySettings() {
        return gRenderQualitySettings;
    }

    void SetRenderQualitySettings(const RenderQualitySettings& settings) {
        gRenderQualitySettings = settings;
        gRenderQualitySettings.viewportScale =
            std::clamp(gRenderQualitySettings.viewportScale, 0.25f, 2.0f);
        gRenderQualitySettings.taaHistoryWeight =
            std::clamp(gRenderQualitySettings.taaHistoryWeight, 0.0f, 0.97f);
        gRenderQualitySettings.taaVarianceClipGamma =
            std::clamp(gRenderQualitySettings.taaVarianceClipGamma, 0.0f, 4.0f);
        gRenderQualitySettings.taaDepthRejection =
            std::clamp(gRenderQualitySettings.taaDepthRejection, 0.0001f, 0.05f);
        gRenderQualitySettings.taaLuminanceRejection =
            std::clamp(gRenderQualitySettings.taaLuminanceRejection, 0.05f, 4.0f);
        gRenderQualitySettings.taaSharpness =
            std::clamp(gRenderQualitySettings.taaSharpness, 0.0f, 1.0f);
        if (static_cast<uint8_t>(gRenderQualitySettings.antiAliasingMode) >
            static_cast<uint8_t>(RenderAntiAliasingMode::DLSS)) {
            gRenderQualitySettings.antiAliasingMode = RenderAntiAliasingMode::Off;
        }
        if (static_cast<uint8_t>(gRenderQualitySettings.dlssQualityMode) >
            static_cast<uint8_t>(DlssQualityMode::UltraPerformance)) {
            gRenderQualitySettings.dlssQualityMode = DlssQualityMode::Quality;
        }
        if (static_cast<uint8_t>(gRenderQualitySettings.volumetricLightingQuality) >
            static_cast<uint8_t>(VolumetricLightingQuality::High)) {
            gRenderQualitySettings.volumetricLightingQuality =
                VolumetricLightingQuality::Balanced;
        }
        if (static_cast<uint8_t>(gRenderQualitySettings.forwardCostMode) >
            static_cast<uint8_t>(ForwardShadingCostMode::NoMaterialExtras)) {
            gRenderQualitySettings.forwardCostMode = ForwardShadingCostMode::Full;
        }
        if (static_cast<uint8_t>(gRenderQualitySettings.lightProbeVolumeSampling) >
            static_cast<uint8_t>(LightProbeVolumeSamplingMode::Off)) {
            gRenderQualitySettings.lightProbeVolumeSampling =
                LightProbeVolumeSamplingMode::FastSmooth;
        }
    }

    const char* RenderResolutionPresetLabel(RenderResolutionPreset preset) {
        switch (preset) {
        case RenderResolutionPreset::Viewport: return "Viewport";
        case RenderResolutionPreset::P720: return "1280 x 720";
        case RenderResolutionPreset::P1080: return "1920 x 1080";
        case RenderResolutionPreset::P1440: return "2560 x 1440";
        case RenderResolutionPreset::P2160: return "3840 x 2160";
        default: return "Unknown";
        }
    }

    const char* WindowPresentationModeLabel(WindowPresentationMode mode) {
        switch (mode) {
        case WindowPresentationMode::Windowed: return "Windowed";
        case WindowPresentationMode::BorderlessWindow: return "Borderless";
        case WindowPresentationMode::Fullscreen: return "Fullscreen";
        default: return "Unknown";
        }
    }

    const char* GeometryPipelineModeLabel(GeometryPipelineMode mode) {
        switch (mode) {
        case GeometryPipelineMode::MeshShader: return "Mesh Shader";
        case GeometryPipelineMode::TraditionalVsPs: return "VS + PS";
        case GeometryPipelineMode::AutoFallback: return "Auto Fallback";
        default: return "Unknown";
        }
    }

    const char* ForwardShadingCostModeLabel(ForwardShadingCostMode mode) {
        switch (mode) {
        case ForwardShadingCostMode::Full: return "Full";
        case ForwardShadingCostMode::AlbedoOnly: return "Albedo Only";
        case ForwardShadingCostMode::NoNormalMap: return "No Normal Map";
        case ForwardShadingCostMode::NoShadow: return "No Shadow";
        case ForwardShadingCostMode::NoSsao: return "No SSAO";
        case ForwardShadingCostMode::NoMaterialExtras: return "No Material Extras";
        default: return "Unknown";
        }
    }

    const char* LightProbeVolumeSamplingModeLabel(LightProbeVolumeSamplingMode mode) {
        switch (mode) {
        case LightProbeVolumeSamplingMode::FastSmooth: return "Fast Smooth";
        case LightProbeVolumeSamplingMode::FullTrilinear: return "Full Trilinear";
        case LightProbeVolumeSamplingMode::Off: return "Off";
        default: return "Unknown";
        }
    }

    const char* RenderAntiAliasingModeLabel(RenderAntiAliasingMode mode) {
        switch (mode) {
        case RenderAntiAliasingMode::Off: return "Off";
        case RenderAntiAliasingMode::FXAA: return "FXAA";
        case RenderAntiAliasingMode::TAA: return "TAA";
        case RenderAntiAliasingMode::DLAA: return "DLAA";
        case RenderAntiAliasingMode::DLSS: return "DLSS";
        default: return "Unknown";
        }
    }

    const char* DlssQualityModeLabel(DlssQualityMode mode) {
        switch (mode) {
        case DlssQualityMode::Quality: return "Quality";
        case DlssQualityMode::Balanced: return "Balanced";
        case DlssQualityMode::Performance: return "Performance";
        case DlssQualityMode::UltraPerformance: return "Ultra Performance";
        default: return "Unknown";
        }
    }

    const char* VolumetricLightingQualityLabel(VolumetricLightingQuality quality) {
        switch (quality) {
        case VolumetricLightingQuality::Low: return "Low";
        case VolumetricLightingQuality::Balanced: return "Balanced";
        case VolumetricLightingQuality::High: return "High";
        default: return "Unknown";
        }
    }

    bool IsTemporalAntiAliasingMode(RenderAntiAliasingMode mode) {
        return mode == RenderAntiAliasingMode::TAA;
    }

    bool UsesTemporalJitter(RenderAntiAliasingMode mode) {
        return
            mode == RenderAntiAliasingMode::TAA ||
            mode == RenderAntiAliasingMode::DLAA ||
            mode == RenderAntiAliasingMode::DLSS;
    }

    bool IsDlaaAntiAliasingMode(RenderAntiAliasingMode mode) {
        return mode == RenderAntiAliasingMode::DLAA;
    }

    bool IsDlssAntiAliasingMode(RenderAntiAliasingMode mode) {
        return mode == RenderAntiAliasingMode::DLSS;
    }

    bool IsFxaaAntiAliasingMode(RenderAntiAliasingMode mode) {
        return mode == RenderAntiAliasingMode::FXAA;
    }

    bool IsAntiAliasingModeAvailable(RenderAntiAliasingMode mode) {
        if (mode == RenderAntiAliasingMode::DLAA ||
            mode == RenderAntiAliasingMode::DLSS) {
            return UPSCALING::IsStreamlineDlssAvailable();
        }
        return true;
    }

    bool IsFixedRenderResolutionPreset(RenderResolutionPreset preset) {
        return preset != RenderResolutionPreset::Viewport;
    }

    RenderResolution ResolveFixedRenderResolution(RenderResolutionPreset preset) {
        switch (preset) {
        case RenderResolutionPreset::P720: return { 1280, 720 };
        case RenderResolutionPreset::P1080: return { 1920, 1080 };
        case RenderResolutionPreset::P1440: return { 2560, 1440 };
        case RenderResolutionPreset::P2160: return { 3840, 2160 };
        case RenderResolutionPreset::Viewport:
        default: return { 0, 0 };
        }
    }

    RenderResolution ResolveSceneOutputResolution(
        const RenderQualitySettings& settings,
        int viewportWidth,
        int viewportHeight) {

        if (IsFixedRenderResolutionPreset(settings.sceneResolution)) {
            return ResolveFixedRenderResolution(settings.sceneResolution);
        }

        const float scale = std::clamp(settings.viewportScale, 0.25f, 2.0f);
        return ClampResolution({
            static_cast<int>(static_cast<float>((std::max)(viewportWidth, 1)) * scale + 0.5f),
            static_cast<int>(static_cast<float>((std::max)(viewportHeight, 1)) * scale + 0.5f)
        });
    }

} // namespace HIKARI::RENDER3D
