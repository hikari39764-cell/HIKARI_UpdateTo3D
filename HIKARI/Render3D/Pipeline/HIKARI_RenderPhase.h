#pragma once

namespace HIKARI::RENDER3D {

    enum class RenderPhase {
        Opaque = 0,
        DepthAware,
        SceneColorAware,
        Transparent,
        Distortion,
        Overlay,
        Debug,
    };

} // namespace HIKARI::RENDER3D
