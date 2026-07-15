#include "DimWindow.h"
#include "Theme.h"
#include "Log.h"
#include <windowsx.h>

namespace {
const wchar_t* kClass = L"MindfulDimWindow";
}

bool DimWindow::create(HINSTANCE hInst, GraphicsDevice* gfx, const RECT& workArea) {
    gfx_  = gfx;
    work_ = workArea;

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = &DimWindow::WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    const int w = work_.right - work_.left;
    const int h = work_.bottom - work_.top;

    // Dumb dimmer: NOACTIVATE (never steals focus) + TOOLWINDOW + topmost.
    const DWORD exStyle = WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW |
                          WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST;
    hwnd_ = CreateWindowExW(exStyle, kClass, L"MindfulDim", WS_POPUP,
                            work_.left, work_.top, w, h,
                            nullptr, nullptr, hInst, this);
    if (!hwnd_) {
        Log::write(L"[dim] CreateWindowEx failed err=%lu", GetLastError());
        return false;
    }
    if (!comp_.init(gfx_, hwnd_, static_cast<UINT>(w), static_cast<UINT>(h))) {
        Log::write(L"[dim] composition init failed");
        return false;
    }
    Log::write(L"[dim] dimmer created %dx%d at (%ld,%ld)", w, h, work_.left, work_.top);
    return true;
}

void DimWindow::render() {
    ID2D1DeviceContext* dc = comp_.begin();
    dc->Clear(D2D1::ColorF(0, 0, 0, 0));
    ComPtr<ID2D1SolidColorBrush> b;
    dc->CreateSolidColorBrush(Theme::dimColor(alpha_), &b);
    dc->FillRectangle(D2D1::RectF(0, 0, (float)comp_.width(), (float)comp_.height()),
                      b.Get());
    comp_.end();
}

void DimWindow::show() {
    ShowWindow(hwnd_, SW_SHOWNA); // show without activating
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    render();
}

void DimWindow::showNA() {
    ShowWindow(hwnd_, SW_SHOWNA);
    SetWindowPos(hwnd_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
}

void DimWindow::hide() { ShowWindow(hwnd_, SW_HIDE); }

void DimWindow::setAlpha(float a) {
    if (a < 0.f) a = 0.f;
    if (a == alpha_) return;
    alpha_ = a;
    render();
}

LRESULT CALLBACK DimWindow::WndProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        auto* self = static_cast<DimWindow*>(cs->lpCreateParams);
        self->hwnd_ = h;
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<DimWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self) return self->handle(msg, w, l);
    return DefWindowProcW(h, msg, w, l);
}

LRESULT DimWindow::handle(UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps; BeginPaint(hwnd_, &ps); render(); EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE; // eat clicks without ever activating
    case WM_LBUTTONDOWN:
        Log::write(L"[dim] click eaten at (%d,%d) -> pulse panel",
                   GET_X_LPARAM(l), GET_Y_LPARAM(l));
        if (onClick) onClick();
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, w, l);
}

DimWindow::~DimWindow() {
    if (hwnd_) DestroyWindow(hwnd_);
}
