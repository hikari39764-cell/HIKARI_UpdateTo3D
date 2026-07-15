#pragma once

namespace HIKARI {

    class DebugCameraController3D;
    struct DebugWindowState;
    namespace EDITOR {
        class EditorToolHost;
        class EditorWorkspaceHost;
    }

    class DebugMenuBar {
    public:
        void Draw(
            DebugWindowState& windows,
            EDITOR::EditorToolHost& toolHost,
            EDITOR::EditorWorkspaceHost& workspaceHost,
            DebugCameraController3D& debugCamera,
            bool& environmentLightingEnabled,
            bool& resetDockingLayoutRequested) const;
    };

} // namespace HIKARI
