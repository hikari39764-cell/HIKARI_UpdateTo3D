#pragma once

#include <cstdint>

namespace HIKARI {

struct FrameContext {
    float rawDt = 0.0f;
    float gameDt = 0.0f;
    float unscaledDt = 0.0f;
    float fixedDt = 1.0f / 60.0f;
    float gameTimeScale = 1.0f;
    bool paused = false;
    uint64_t frameIndex = 0;
};

} // namespace HIKARI
