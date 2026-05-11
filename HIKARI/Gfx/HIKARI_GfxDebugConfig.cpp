#include "HIKARI_GfxDebugConfig.h"

namespace HIKARI::GFX {
    namespace {
        GfxDebugConfig gConfig{};
    }

    const GfxDebugConfig& GetGfxDebugConfig() {
        return gConfig;
    }

    void SetGfxDebugConfig(const GfxDebugConfig& config) {
        gConfig = config;
    }
}
