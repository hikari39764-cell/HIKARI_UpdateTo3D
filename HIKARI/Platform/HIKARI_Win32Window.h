#pragma once

#include <windows.h>
#include <functional>

namespace HIKARI::PLATFORM {

class Win32Window {
public:
    using ResizeCallback = std::function<void(int, int)>;

    bool Initialize(const wchar_t* title, int width, int height, bool resizable = true);
    void Shutdown();

    bool PumpMessages();
    float ConsumeMouseWheelDelta();

    HWND GetHWND() const { return hwnd_; }
    int Width() const { return width_; }
    int Height() const { return height_; }
    bool IsMinimized() const { return isMinimized_; }

    void SetResizeCallback(ResizeCallback cb) { onResize_ = std::move(cb); }

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);
    LRESULT WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam);

private:
    HWND hwnd_{};
    HINSTANCE hInstance_{};
    int width_{};
    int height_{};
    bool isMinimized_{};
    bool running_{};
    float mouseWheelDelta_{};
    ResizeCallback onResize_{};
};

} // namespace HIKARI::PLATFORM
