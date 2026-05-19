#pragma once

#include <cstdint>

namespace HIKARI::RENDER3D {

    struct TextureHandle {
        uint32_t id = 0;
    };

    struct RenderTargetHandle {
        uint32_t id = 0;
    };

    struct DepthTargetHandle {
        uint32_t id = 0;
    };

} // namespace HIKARI::RENDER3D
