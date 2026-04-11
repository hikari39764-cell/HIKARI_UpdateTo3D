#pragma once

namespace HIKARI {

    class DebugCameraController3D;
    struct DebugWindowState;

    class DebugMenuBar {
    public:
        void Draw(DebugWindowState& windows, DebugCameraController3D& debugCamera, bool& environmentLightingEnabled) const;
    };

} // namespace HIKARI
