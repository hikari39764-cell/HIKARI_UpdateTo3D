#pragma once

#include "Gfx/HIKARI_GfxContext.h"

namespace HIKARI::SERVICES {

struct GraphicsService {
    static void SetContext(const GFX::Context& context) {
        (void)context;
    }
};

} // namespace HIKARI::SERVICES
