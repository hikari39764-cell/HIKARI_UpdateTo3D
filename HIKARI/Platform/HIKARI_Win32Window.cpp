#include "HIKARI_Win32Window.h"

#include <cstdio>
#include <imgui_impl_win32.h>

namespace HIKARI::PLATFORM {

bool Win32Window::Initialize(const wchar_t* title, int width, int height, bool resizable) {
    hInstance_ = GetModuleHandleW(nullptr);
    width_ = width;
    height_ = height;

    WNDCLASSW wc{};
    wc.lpfnWndProc = StaticWndProc;
    wc.hInstance = hInstance_;
    wc.lpszClassName = L"HIKARI_WindowClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    const ATOM cls = RegisterClassW(&wc);
    if (cls == 0) {
        const DWORD err = GetLastError();
        if (err != ERROR_CLASS_ALREADY_EXISTS) {
            char msg[256]{};
            std::snprintf(msg, sizeof(msg), "[Win32Window] RegisterClassW failed. err=%lu\n", static_cast<unsigned long>(err));
            OutputDebugStringA(msg);
            return false;
        }
    }

    DWORD style = WS_OVERLAPPEDWINDOW;
    if (!resizable) {
        style &= ~WS_THICKFRAME;
        style &= ~WS_MAXIMIZEBOX;
    }

    RECT rect{ 0, 0, width, height };
    AdjustWindowRect(&rect, style, FALSE);

    hwnd_ = CreateWindowExW(
        0,
        wc.lpszClassName,
        title,
        style,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        hInstance_,
        this);

    if (!hwnd_) {
        char msg[256]{};
        std::snprintf(msg, sizeof(msg), "[Win32Window] CreateWindowExW failed. err=%lu\n", static_cast<unsigned long>(GetLastError()));
        OutputDebugStringA(msg);
        return false;
    }

    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    running_ = true;
    return true;
}

void Win32Window::Shutdown() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
        hwnd_ = nullptr;
    }
    running_ = false;
}

bool Win32Window::PumpMessages() {
    MSG msg{};
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT) {
            running_ = false;
            return false;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return running_;
}

float Win32Window::ConsumeMouseWheelDelta() {
    const float delta = mouseWheelDelta_;
    mouseWheelDelta_ = 0.0f;
    return delta;
}

LRESULT CALLBACK Win32Window::StaticWndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    Win32Window* window = nullptr;
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        window = reinterpret_cast<Win32Window*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
    } else {
        window = reinterpret_cast<Win32Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (window) {
        return window->WndProc(hwnd, msg, wparam, lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT Win32Window::WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (ImGui_ImplWin32_WndProcHandler(hwnd, msg, wparam, lparam)) {
        return TRUE;
    }

    switch (msg) {
    case WM_SIZE:
        width_ = LOWORD(lparam);
        height_ = HIWORD(lparam);
        isMinimized_ = (wparam == SIZE_MINIMIZED);
        if (!isMinimized_ && onResize_) {
            onResize_(width_, height_);
        }
        break;
    case WM_DESTROY:
        running_ = false;
        PostQuitMessage(0);
        return 0;
    case WM_MOUSEWHEEL:
        mouseWheelDelta_ += static_cast<float>(GET_WHEEL_DELTA_WPARAM(wparam)) / static_cast<float>(WHEEL_DELTA);
        return 0;
    default:
        break;
    }
    // NOTE: 必ず「今処理中の hwnd」を渡す。CreateWindow 直後などで hwnd_ が未確定でも安全。
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

} // namespace HIKARI::PLATFORM
