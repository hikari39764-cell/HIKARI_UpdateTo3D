#pragma once

namespace HIKARI::GFX {

    struct GfxDebugConfig {
        bool enableDebugLayer = true;
        bool enableGpuBasedValidation = false;
        bool enableGpuFrameProfiler = true;
        bool enableGpuFrameProfilerWithDebugLayer = false;
        bool enableClusterGpuCullCounterReadback = false;
        bool enableClusterGpuCullDebugCounters = false;
        bool enableInfoQueueBreakOnError = false;
        bool enableInfoQueueBreakOnWarning = false;
        bool dumpInfoQueueOnFrameEnd = false;
        bool verboseRenderTargetLog = false;
        bool verbosePostLog = false;
    };

    const GfxDebugConfig& GetGfxDebugConfig();
    void SetGfxDebugConfig(const GfxDebugConfig& config);

}
