#pragma once

#include <cstdint>

namespace HIKARI {
    class Camera3D;
    struct SceneEnvironment;
}

namespace HIKARI::RENDER3D::PIPELINE {

    bool RenderMeshLightingFrame(
        const Camera3D& camera,
        const SceneEnvironment& environment);

    bool RenderMeshCaptureOpaqueFrame(
        const Camera3D& camera,
        const SceneEnvironment& environment,
        uint32_t width,
        uint32_t height);

} // namespace HIKARI::RENDER3D::PIPELINE
