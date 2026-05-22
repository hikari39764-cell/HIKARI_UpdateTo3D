#pragma once

namespace HIKARI::GFX {

    struct GfxDebugConfig {
        bool enableDebugLayer = true;
        bool enableGpuBasedValidation = false;
        bool enableInfoQueueBreakOnError = true;
        bool enableInfoQueueBreakOnWarning = true;
        bool dumpInfoQueueOnFrameEnd = true;
        bool verboseRenderTargetLog = false;
        bool verbosePostLog = false;
    };

    const GfxDebugConfig& GetGfxDebugConfig();
    void SetGfxDebugConfig(const GfxDebugConfig& config);

}
