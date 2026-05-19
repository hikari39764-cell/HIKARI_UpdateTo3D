#pragma once

namespace HIKARI::EDITOR {

    void SetGameViewportInputRect(float x, float y, float width, float height, bool windowFocused);
    void ClearGameViewportInputRect();

    bool HasGameViewportInputRect();
    bool IsGameViewportMouseHovered();
    bool IsGameViewportMouseInputActive();
    bool IsGameViewportKeyboardInputActive();

} // namespace HIKARI::EDITOR
