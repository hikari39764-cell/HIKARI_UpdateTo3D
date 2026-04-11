#pragma once

#include "HIKARI_Math3D.h"

namespace HIKARI {

    struct DebugWindowState;

    namespace LIGHTDEBUGDRAW {

        void SubmitDirectionalLightArrow(const MATH::Vec3& directionalDir, const DebugWindowState& debugWindowState);

    } // namespace LIGHTDEBUGDRAW

} // namespace HIKARI
