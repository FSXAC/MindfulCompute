#pragma once
// Shell_NotifyIcon tray presence with a context menu and, crucially, a runtime
// icon-bitmap swap: the countdown "minutes" number is rendered into an HICON via
// GDI and pushed with NIM_MODIFY. (The tray cannot show live text -- the one
// genuine UX compromise from the brief; M:SS goes in the tooltip.)
#include <windows.h>
#include <shellapi.h>
#include <functional>
#include <climits>

class TrayIcon {
public:
    // callbackMsg is a WM_APP+ message posted to msgWindow on tray interaction.
    bool create(HINSTANCE hInst, HWND msgWindow, UINT callbackMsg, UINT quitMenuId);

    // Set tooltip to `tip` and, only when `number` differs from the last icon
    // rendered, re-render the icon bitmap. The tooltip (M:SS) can change every
    // second cheaply (NIF_TIP only); the icon (minutes) is re-rendered via GDI
    // only when the minute value actually changes -- so a 1 Hz running tick
    // costs no per-second GDI work between minute boundaries, and idle costs
    // nothing at all (main gates the countdown timer off while idle).
    void setBadge(int number, const wchar_t* tip);

    // Optional: a fixed icon to use as the idle "leaf" base (e.g. the app icon
    // loaded from the exe's resources). Pass nullptr to keep the drawn leaf.
    void setIdleIcon(HICON icon) { idleIcon_ = icon; }

    // Handle the tray callback (right-click -> context menu).
    void onCallback(WPARAM wParam, LPARAM lParam);

    void destroy();

    std::function<void()> onQuit;
    std::function<void()> onLeftClick;   // bring the panel/break to front
    // Populate the right-click context menu with the current per-phase items.
    // If unset, a bare "Quit" fallback is used.
    std::function<void(HMENU)> onBuildMenu;
    ~TrayIcon();

private:
    HICON renderBadgeIcon(int number);
    void  showMenu();

    NOTIFYICONDATAW nid_{};
    HWND      msgWindow_ = nullptr;
    HINSTANCE hInst_     = nullptr;
    HICON     current_   = nullptr;
    HICON     idleIcon_  = nullptr;   // shared app-icon HICON for the -1 (idle) state, if set
    int       lastNumber_ = INT_MIN;  // last number rendered into current_ (sentinel = none yet)
    UINT      quitId_    = 0;
    bool      added_     = false;
};
