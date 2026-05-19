#include "HIKARI_EditorViewportInput.h"

#if defined(_DEBUG)
#include "imgui.h"
#endif

namespace HIKARI::EDITOR {

    namespace {
        struct GameViewportInputState {
            bool hasRect = false;
            bool windowFocused = false;
            bool mouseCaptured = false;
            float x = 0.0f;
            float y = 0.0f;
            float width = 0.0f;
            float height = 0.0f;
        };

        GameViewportInputState gGameViewportInput{};

#if defined(_DEBUG)
        bool HasImGuiContext()
        {
            return ImGui::GetCurrentContext() != nullptr;
        }

        bool IsMouseInsideGameViewport()
        {
            if (!gGameViewportInput.hasRect || !HasImGuiContext()) {
                return false;
            }

            const ImVec2 mouse = ImGui::GetIO().MousePos;
            return
                mouse.x >= gGameViewportInput.x &&
                mouse.y >= gGameViewportInput.y &&
                mouse.x < gGameViewportInput.x + gGameViewportInput.width &&
                mouse.y < gGameViewportInput.y + gGameViewportInput.height;
        }

        bool IsCameraMouseButtonDown()
        {
            return
                ImGui::IsMouseDown(ImGuiMouseButton_Right) ||
                ImGui::IsMouseDown(ImGuiMouseButton_Middle);
        }

        bool IsCameraMouseButtonClicked()
        {
            return
                ImGui::IsMouseClicked(ImGuiMouseButton_Right) ||
                ImGui::IsMouseClicked(ImGuiMouseButton_Middle);
        }
#endif
    }

    void SetGameViewportInputRect(float x, float y, float width, float height, bool windowFocused)
    {
#if defined(_DEBUG)
        if (width <= 1.0f || height <= 1.0f) {
            ClearGameViewportInputRect();
            return;
        }

        gGameViewportInput.hasRect = true;
        gGameViewportInput.windowFocused = windowFocused;
        gGameViewportInput.x = x;
        gGameViewportInput.y = y;
        gGameViewportInput.width = width;
        gGameViewportInput.height = height;
#else
        (void)x;
        (void)y;
        (void)width;
        (void)height;
        (void)windowFocused;
#endif
    }

    void ClearGameViewportInputRect()
    {
        gGameViewportInput = {};
    }

    bool HasGameViewportInputRect()
    {
        return gGameViewportInput.hasRect;
    }

    bool IsGameViewportMouseHovered()
    {
#if defined(_DEBUG)
        return IsMouseInsideGameViewport();
#else
        return false;
#endif
    }

    bool IsGameViewportMouseInputActive()
    {
#if defined(_DEBUG)
        if (!gGameViewportInput.hasRect || !HasImGuiContext()) {
            return false;
        }

        const bool hovered = IsMouseInsideGameViewport();
        if (!IsCameraMouseButtonDown()) {
            gGameViewportInput.mouseCaptured = false;
        } else if (hovered && IsCameraMouseButtonClicked()) {
            gGameViewportInput.mouseCaptured = true;
        }

        if (IsCameraMouseButtonDown()) {
            return gGameViewportInput.mouseCaptured;
        }

        return hovered;
#else
        return false;
#endif
    }

    bool IsGameViewportKeyboardInputActive()
    {
#if defined(_DEBUG)
        if (!gGameViewportInput.hasRect || !HasImGuiContext()) {
            return false;
        }

        const ImGuiIO& io = ImGui::GetIO();
        if (io.WantTextInput) {
            return false;
        }

        return
            gGameViewportInput.mouseCaptured ||
            (gGameViewportInput.windowFocused && IsMouseInsideGameViewport());
#else
        return false;
#endif
    }

} // namespace HIKARI::EDITOR
