#pragma once

#include "Render3D/Temporal/HIKARI_TemporalFrameState.h"

namespace HIKARI::RENDER3D::DEBUGVIEW {

    void SetMotionVectorDebugScale(float pixelsToColorScale);

    bool ExecuteMotionVectorDebugView(
        const RENDER3D::TEMPORAL::TemporalTextureView& motionVectors);

    void ShutdownRenderDebugViewPasses();

} // namespace HIKARI::RENDER3D::DEBUGVIEW
