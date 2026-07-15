#pragma once

namespace HIKARI {

    class DebugCameraController3D;
    struct DebugWindowState;
    namespace EDITOR {
        class EditorToolHost;
    }

    class DebugMenuBar {
    public:
        void Draw(
            DebugWindowState& windows,
            EDITOR::EditorToolHost& toolHost,
            DebugCameraController3D& debugCamera,
            bool& environmentLightingEnabled,
            bool& resetDockingLayoutRequested) const;
    };

} // namespace HIKARI
