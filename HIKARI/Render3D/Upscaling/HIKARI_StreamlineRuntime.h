#pragma once

#include <cstdint>
#include <string>

#include "Gfx/HIKARI_GfxContext.h"
#include "Render3D/Settings/HIKARI_RenderQualitySettings.h"
#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

namespace HIKARI {
    class RenderTarget2D;
}

namespace HIKARI::RENDER3D::UPSCALING {

    enum class StreamlineDlssMode : uint8_t {
        Off = 0,
        Dlaa,
        Quality,
        Balanced,
        Performance,
        UltraPerformance,
    };

    struct StreamlineOptimalSettings {
        bool valid = false;
        uint32_t optimalRenderWidth = 0;
        uint32_t optimalRenderHeight = 0;
        uint32_t minRenderWidth = 0;
        uint32_t minRenderHeight = 0;
        uint32_t maxRenderWidth = 0;
        uint32_t maxRenderHeight = 0;
    };

    enum class StreamlineRuntimeStatus : uint8_t {
        NotCompiled = 0,
        AwaitingInitialization,
        SignatureRejected,
        InitializationFailed,
        AwaitingDevice,
        DeviceFailed,
        FeatureUnsupported,
        Ready,
        RuntimeFailure,
    };

    struct StreamlineDebugStats {
        bool sdkCompiled = false;
        bool initialized = false;
        bool deviceAttached = false;
        bool dlssSupported = false;
        bool frameTokenReady = false;
        bool constantsSubmitted = false;
        bool dlssRequested = false;
        bool dlssEvaluated = false;
        bool fallbackUsed = false;
        bool outputReady = false;
        StreamlineDlssMode mode = StreamlineDlssMode::Off;
        uint32_t renderWidth = 0;
        uint32_t renderHeight = 0;
        uint32_t outputWidth = 0;
        uint32_t outputHeight = 0;
        uint32_t optimalRenderWidth = 0;
        uint32_t optimalRenderHeight = 0;
        uint32_t minRenderWidth = 0;
        uint32_t minRenderHeight = 0;
        uint32_t maxRenderWidth = 0;
        uint32_t maxRenderHeight = 0;
        uint64_t frameIndex = 0;
        uint64_t evaluationCount = 0;
        uint64_t failureCount = 0;
        uint64_t estimatedVramBytes = 0;
        StreamlineRuntimeStatus status = StreamlineRuntimeStatus::NotCompiled;
        std::string sdkVersion{};
        std::string lastOperation{};
        std::string lastResult{};
    };

    bool InitializeStreamlineEarly();
    bool AttachStreamlineDevice(ID3D12Device* device);
    void UpdateStreamlineContext(const GFX::Context& context);
    bool BeginStreamlineFrame(
        const TEMPORAL::TemporalFrameState& frame,
        StreamlineDlssMode mode);
    RenderTarget2D* ExecuteStreamlineDlss(
        const TEMPORAL::TemporalInputs& inputs,
        StreamlineDlssMode mode);
    void MarkStreamlineDlssFallback();
    void ShutdownStreamline();

    StreamlineDlssMode ResolveStreamlineDlssMode(
        const RenderQualitySettings& settings);
    bool IsStreamlineDlssSuperResolutionMode(StreamlineDlssMode mode);
    bool QueryStreamlineDlssOptimalSettings(
        StreamlineDlssMode mode,
        uint32_t outputWidth,
        uint32_t outputHeight,
        StreamlineOptimalSettings& outSettings);
    bool IsStreamlineDlssAvailable();
    const StreamlineDebugStats& GetStreamlineDebugStats();
    const char* ToString(StreamlineDlssMode mode);
    const char* ToString(StreamlineRuntimeStatus status);

} // namespace HIKARI::RENDER3D::UPSCALING
