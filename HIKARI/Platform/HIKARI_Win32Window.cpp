#include "HIKARI_Win32Window.h"

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
    RegisterClassW(&wc);

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

    if (!hwnd_) { return false; }

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
        return window->WndProc(msg, wparam, lparam);
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}

LRESULT Win32Window::WndProc(UINT msg, WPARAM wparam, LPARAM lparam) {
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
    default:
        break;
    }
    return DefWindowProcW(hwnd_, msg, wparam, lparam);
}

} // namespace HIKARI::PLATFORM
