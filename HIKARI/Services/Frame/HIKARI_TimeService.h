#pragma once

#include "HIKARI_FrameContext.h"

namespace HIKARI::SERVICES {

class TimeService {
public:
    void BeginFrame(float rawDt) {
        frame_.rawDt = rawDt;
        frame_.unscaledDt = rawDt;
        frame_.gameDt = rawDt * frame_.gameTimeScale;
        ++frame_.frameIndex;
    }

    FrameContext& MutableFrame() { return frame_; }
    const FrameContext& GetFrame() const { return frame_; }

private:
    FrameContext frame_{};
};

} // namespace HIKARI::SERVICES
