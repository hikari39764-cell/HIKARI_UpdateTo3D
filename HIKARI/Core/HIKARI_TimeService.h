#pragma once

#include "Core/HIKARI_FrameContext.h"

namespace HIKARI::TIME {

void Reset();
const FrameContext& BeginFrame();
const FrameContext& GetFrameContext();

void SetGameTimeScale(float scale);
float GetGameTimeScale();

void SetPaused(bool paused);
bool IsPaused();

void SetMaxDeltaSeconds(float maxDelta);

} // namespace HIKARI::TIME
