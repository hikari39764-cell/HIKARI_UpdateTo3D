#pragma once

#include <cstdint>

namespace HIKARI::RENDER3D {

    enum class RenderResolutionPreset : uint8_t {
        Viewport = 0,
        P720,
        P1080,
        P1440,
    };

    enum class WindowPresentationMode : uint8_t {
        Windowed = 0,
        BorderlessWindow,
        Fullscreen,
    };

    enum class GeometryPipelineMode : uint8_t {
        MeshShader = 0,
        TraditionalVsPs,
        AutoFallback,
    };

    enum class ForwardShadingCostMode : uint8_t {
        Full = 0,
        AlbedoOnly,
        NoNormalMap,
        NoShadow,
        NoSsao,
        NoMaterialExtras,
    };

    struct RenderResolution {
        int width = 0;
        int height = 0;
    };

    struct RenderQualitySettings {
        RenderResolutionPreset sceneResolution = RenderResolutionPreset::P1080;
        RenderResolutionPreset windowSize = RenderResolutionPreset::P720;
        WindowPresentationMode windowMode = WindowPresentationMode::Windowed;
        GeometryPipelineMode geometryPipeline = GeometryPipelineMode::MeshShader;
        ForwardShadingCostMode forwardCostMode = ForwardShadingCostMode::Full;
        float viewportScale = 1.0f;
    };

    RenderQualitySettings& GetRenderQualitySettings();
    void SetRenderQualitySettings(const RenderQualitySettings& settings);

    const char* RenderResolutionPresetLabel(RenderResolutionPreset preset);
    const char* WindowPresentationModeLabel(WindowPresentationMode mode);
    const char* GeometryPipelineModeLabel(GeometryPipelineMode mode);
    const char* ForwardShadingCostModeLabel(ForwardShadingCostMode mode);

    bool IsFixedRenderResolutionPreset(RenderResolutionPreset preset);
    RenderResolution ResolveFixedRenderResolution(RenderResolutionPreset preset);
    RenderResolution ResolveSceneCaptureResolution(
        const RenderQualitySettings& settings,
        int viewportWidth,
        int viewportHeight);

} // namespace HIKARI::RENDER3D
