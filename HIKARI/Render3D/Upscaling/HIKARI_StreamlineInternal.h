#pragma once

#include <string>

#include "Render2D/HIKARI_RenderTarget2D.h"
#include "Render3D/Upscaling/HIKARI_StreamlineRuntime.h"

#if defined(HIKARI_WITH_STREAMLINE)
#pragma warning(push, 0)
#include <sl.h>
#pragma warning(pop)
#endif

namespace HIKARI::RENDER3D::UPSCALING::INTERNAL {

    inline constexpr const char* kStreamlineSdkVersion = "2.12.0";

    struct StreamlineState {
        GFX::Context context{};
        RenderTarget2D output{};
        DXGI_FORMAT outputFormat = DXGI_FORMAT_UNKNOWN;
        uint32_t outputWidth = 0;
        uint32_t outputHeight = 0;
        StreamlineDebugStats stats{};
#if defined(HIKARI_WITH_STREAMLINE)
        sl::FrameToken* frameToken = nullptr;
        sl::ViewportHandle viewport{ 0u };
        bool optionsConfigured = false;
        StreamlineDlssMode configuredMode = StreamlineDlssMode::Off;
        uint32_t configuredOutputWidth = 0;
        uint32_t configuredOutputHeight = 0;
        bool optimalSettingsCached = false;
        StreamlineDlssMode optimalSettingsMode = StreamlineDlssMode::Off;
        uint32_t optimalSettingsOutputWidth = 0;
        uint32_t optimalSettingsOutputHeight = 0;
        StreamlineOptimalSettings optimalSettings{};
        std::wstring pluginPath{};
        std::wstring logPath{};
#endif
    };

    StreamlineState& GetState();
    void ResetFrameStats(StreamlineState& state, uint64_t frameIndex);

#if defined(HIKARI_WITH_STREAMLINE)
    bool RecordResult(
        StreamlineState& state,
        const char* operation,
        sl::Result result,
        bool countFailure = true);
#endif

} // namespace HIKARI::RENDER3D::UPSCALING::INTERNAL
