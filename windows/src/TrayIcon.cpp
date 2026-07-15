#include "TrayIcon.h"
#include "Theme.h"
#include "Log.h"
#include <shellapi.h>
#include <windowsx.h>

namespace { constexpr UINT kTrayId = 1; }

bool TrayIcon::create(HINSTANCE hInst, HWND msgWindow, UINT callbackMsg, UINT quitMenuId) {
    hInst_     = hInst;
    msgWindow_ = msgWindow;
    quitId_    = quitMenuId;

    current_ = renderBadgeIcon(-1); // -1 => plain "leaf" dot, no number
    lastNumber_ = -1;               // remember what current_ holds, so setBadge can skip re-renders

    nid_.cbSize           = sizeof(nid_);
    nid_.hWnd             = msgWindow;
    nid_.uID              = kTrayId;
    nid_.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid_.uCallbackMessage = callbackMsg;
    nid_.hIcon            = current_;
    wcscpy_s(nid_.szTip, L"MindfulCompute");

    added_ = Shell_NotifyIconW(NIM_ADD, &nid_) != FALSE;
    if (added_) {
        nid_.uVersion = NOTIFYICON_VERSION_4;
        Shell_NotifyIconW(NIM_SETVERSION, &nid_);
    }
    Log::write(L"[tray] NIM_ADD %ls", added_ ? L"succeeded" : L"FAILED");
    return added_;
}

HICON TrayIcon::renderBadgeIcon(int number) {
    const int sz = GetSystemMetrics(SM_CXSMICON); // DPI-scaled (e.g. 16/20/24)

    HDC screen = GetDC(nullptr);
    HDC mem    = CreateCompatibleDC(screen);
    HBITMAP color = CreateCompatibleBitmap(screen, sz, sz);
    HBITMAP mask  = CreateBitmap(sz, sz, 1, 1, nullptr);
    HGDIOBJ oldColor = SelectObject(mem, color);

    // Sage badge fill (whole square -> mask is fully opaque; simple + robust).
    HBRUSH bg = CreateSolidBrush(Theme::trayBadgeRGB());
    RECT rc{ 0, 0, sz, sz };
    FillRect(mem, &rc, bg);
    DeleteObject(bg);

    if (number < 0 && idleIcon_) {
        // A real app icon was supplied for the idle state: use it verbatim
        // (copied, so our cache owns a distinct handle) rather than drawing.
        SelectObject(mem, oldColor);
        DeleteObject(color);
        DeleteObject(mask);
        DeleteDC(mem);
        ReleaseDC(nullptr, screen);
        return (HICON)CopyImage(idleIcon_, IMAGE_ICON, sz, sz, 0);  // independent copy we own
    }

    if (number < 0) {
        // Idle "leaf" placeholder: a lighter dot in the centre.
        HBRUSH dot = CreateSolidBrush(RGB(225, 232, 220));
        HGDIOBJ oldB = SelectObject(mem, dot);
        HGDIOBJ oldP = SelectObject(mem, GetStockObject(NULL_PEN));
        int m = sz / 4;
        Ellipse(mem, m, m, sz - m, sz - m);
        SelectObject(mem, oldB);
        SelectObject(mem, oldP);
        DeleteObject(dot);
    } else {
        wchar_t txt[8];
        _snwprintf_s(txt, _countof(txt), _TRUNCATE, L"%d", number);
        int fh = -(sz - 3);
        HFONT font = CreateFontW(fh, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                                 DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                                 CLEARTYPE_QUALITY, FF_SWISS, L"Segoe UI");
        HGDIOBJ oldF = SelectObject(mem, font);
        SetBkMode(mem, TRANSPARENT);
        SetTextColor(mem, RGB(255, 255, 255));
        DrawTextW(mem, txt, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOCLIP);
        SelectObject(mem, oldF);
        DeleteObject(font);
    }

    // Mask: all zero => whole square opaque.
    HDC memMask = CreateCompatibleDC(screen);
    HGDIOBJ oldMask = SelectObject(memMask, mask);
    RECT mr{ 0, 0, sz, sz };
    FillRect(memMask, &mr, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
    SelectObject(memMask, oldMask);
    DeleteDC(memMask);

    SelectObject(mem, oldColor);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);

    ICONINFO ii{};
    ii.fIcon    = TRUE;
    ii.hbmColor = color;
    ii.hbmMask  = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

void TrayIcon::setBadge(int number, const wchar_t* tip) {
    // Only re-render the HICON when the number (idle=-1, or the countdown's
    // minute value) actually changes. A 1 Hz running tick that stays inside a
    // minute changes only the tooltip -- a cheap NIF_TIP-only NIM_MODIFY, no
    // GDI icon work. This is the crux of "idle/steady CPU ~= 0".
    bool iconChanged = (number != lastNumber_);
    if (iconChanged) {
        HICON fresh = renderBadgeIcon(number);
        if (!fresh) {
            Log::write(L"[tray] renderBadgeIcon(%d) returned null", number);
            // fall through to update the tooltip with the stale icon
            iconChanged = false;
        } else {
            HICON old = current_;
            current_    = fresh;
            lastNumber_ = number;
            if (old) DestroyIcon(old);
        }
    }

    bool tipChanged = (wcsncmp(nid_.szTip, tip, _countof(nid_.szTip)) != 0);
    if (!iconChanged && !tipChanged) return;   // nothing to push this tick

    nid_.uFlags = (iconChanged ? NIF_ICON : 0u) | NIF_TIP;
    nid_.hIcon  = current_;
    wcscpy_s(nid_.szTip, tip);
    BOOL ok = Shell_NotifyIconW(NIM_MODIFY, &nid_);
    if (iconChanged) {
        Log::write(L"[tray] re-rendered icon number=%d tooltip=\"%ls\" NIM_MODIFY=%ls",
                   number, tip, ok ? L"OK" : L"FAIL");
    }
}

void TrayIcon::onCallback(WPARAM, LPARAM lParam) {
    // With NOTIFYICON_VERSION_4 the mouse message is in the low word of lParam.
    UINT ev = LOWORD(lParam);
    if (ev == WM_RBUTTONUP || ev == WM_CONTEXTMENU) {
        showMenu();
    } else if (ev == WM_LBUTTONUP) {
        Log::write(L"[tray] left-click -> bring panel to front");
        if (onLeftClick) onLeftClick();
    }
}

void TrayIcon::showMenu() {
    HMENU menu = CreatePopupMenu();
    if (onBuildMenu) onBuildMenu(menu);
    else AppendMenuW(menu, MF_STRING, quitId_, L"Quit");

    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(msgWindow_); // required so the menu dismisses correctly
    TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, msgWindow_, nullptr);
    PostMessageW(msgWindow_, WM_NULL, 0, 0);
    DestroyMenu(menu);
    Log::write(L"[tray] context menu shown");
}

void TrayIcon::destroy() {
    if (added_) {
        Shell_NotifyIconW(NIM_DELETE, &nid_);
        added_ = false;
        Log::write(L"[tray] NIM_DELETE");
    }
    if (current_) { DestroyIcon(current_); current_ = nullptr; }
}

TrayIcon::~TrayIcon() { destroy(); }
