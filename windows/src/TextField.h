#pragma once
// Hosts a classic Win32 EDIT control for the intention/reflection input.
//
// IMPORTANT Phase 0 finding: a child HWND does NOT composite over a
// DirectComposition swap-chain surface (with or without NOREDIRECTIONBITMAP) --
// its pixels never reach the screen, though its text still round-trips via
// WM_GETTEXT. So the EDIT lives in its own thin, OPAQUE top-level window that is
// *owned* by the overlay and floated exactly over the panel's field region.
// DWM composites top-level windows independently, so the control renders. The
// field is opaque chrome anyway (no translucency there), so nothing is lost.
//
// Styling is still done the prescribed way: WM_CTLCOLOREDIT (colors) +
// SetWindowFont (a serif face). Enter submits; Esc is forwarded to the overlay.
#include <windows.h>
#include <string>
#include <functional>

class TextField {
public:
    // owner = the overlay HWND (the field host is owned by it and floats above).
    // screenRect is the field rectangle in SCREEN pixels. fontPx is the EDIT
    // font height in PHYSICAL pixels (caller scales by DPI), since the host is a
    // physical-pixel window.
    bool create(HWND owner, HINSTANCE hInst, int controlId,
                const RECT& screenRect, int fontPx = 15, int cornerRadiusPx = 11);
    void setScreenRect(const RECT& screenRect);
    // Corner radius of the host's rounded region, in PHYSICAL pixels. Kept in
    // sync with the D2D field chrome's radius (kFieldRadius * DPI scale) so the
    // opaque host and the border drawn around it share the same rounded corners.
    void setCornerRadiusPx(int px) { radiusPx_ = (px < 0) ? 0 : px; }
    void showAndFocus();
    void hide();
    void setText(const std::wstring& s);
    std::wstring text() const;
    bool focused() const { return focused_; }

    HWND editHwnd() const { return edit_; }
    HWND hostHwnd() const { return host_; }

    std::function<void()>     onSubmit;        // Enter pressed
    std::function<void()>     onEscape;        // Esc pressed (overlay dismiss)
    std::function<void()>     onChange;        // EN_CHANGE (text edited)
    std::function<void(bool)> onFocusChanged;  // EN_SETFOCUS / EN_KILLFOCUS

    ~TextField();

private:
    static LRESULT CALLBACK HostProc(HWND, UINT, WPARAM, LPARAM);
    static LRESULT CALLBACK EditProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    LRESULT hostHandle(UINT, WPARAM, LPARAM);

    HWND   host_ = nullptr;
    HWND   edit_ = nullptr;
    HFONT  font_ = nullptr;
    HBRUSH bg_   = nullptr;
    bool   focused_ = false;
    int    editPadX_ = 12;
    int    editPadY_ = 10;
    int    radiusPx_ = 11;   // host rounded-corner radius (physical px, DPI-scaled)
};
