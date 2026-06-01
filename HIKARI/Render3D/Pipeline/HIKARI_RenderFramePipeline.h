#pragma once

namespace HIKARI {
    class Camera3D;
    struct SceneEnvironment;
}

namespace HIKARI::RENDER3D::PIPELINE {

    bool RenderMeshLightingFrame(
        const Camera3D& camera,
        const SceneEnvironment& environment);

} // namespace HIKARI::RENDER3D::PIPELINE
