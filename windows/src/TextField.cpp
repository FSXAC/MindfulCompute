#include "TextField.h"
#include "Theme.h"
#include "Log.h"
#include <commctrl.h>

namespace {
const wchar_t* kHostClass = L"MindfulFieldHost";
}

bool TextField::create(HWND owner, HINSTANCE hInst, int controlId,
                       const RECT& sr, int fontPx) {
    editPadX_ = fontPx * 4 / 5;   // horizontal inset scales with the font
    editPadY_ = fontPx * 3 / 5;   // vertical inset centres the single line
    static bool registered = false;
    if (!registered) {
        WNDCLASSEXW wc{};
        wc.cbSize        = sizeof(wc);
        wc.lpfnWndProc   = &TextField::HostProc;
        wc.hInstance     = hInst;
        wc.hCursor       = LoadCursor(nullptr, IDC_IBEAM);
        wc.hbrBackground = nullptr; // we paint the field fill ourselves
        wc.lpszClassName = kHostClass;
        RegisterClassExW(&wc);
        registered = true;
    }

    const int w = sr.right - sr.left;
    const int h = sr.bottom - sr.top;

    // Opaque, topmost tool window. NOT NOACTIVATE: it must take focus so its
    // EDIT receives keystrokes. NOTE: it is created UNOWNED -- a
    // WS_EX_NOREDIRECTIONBITMAP window (the overlay) cannot be an owner
    // (CreateWindowEx fails with ERROR_INVALID_WINDOW_HANDLE). Both windows are
    // topmost and the host is brought to the foreground, so it sits above the
    // overlay's panel. (unused: owner)
    (void)owner;
    host_ = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST, kHostClass, L"",
                            WS_POPUP, sr.left, sr.top, w, h,
                            nullptr, nullptr, hInst, this);
    if (!host_) {
        Log::write(L"[edit] field host CreateWindowEx failed err=%lu", GetLastError());
        return false;
    }
    // Rounded corners to match the D2D field chrome.
    SetWindowRgn(host_, CreateRoundRectRgn(0, 0, w + 1, h + 1, 12, 12), FALSE);

    edit_ = CreateWindowExW(
        0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT,
        editPadX_, editPadY_, w - 2 * editPadX_, h - 2 * editPadY_,
        host_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(controlId)),
        hInst, nullptr);
    if (!edit_) {
        Log::write(L"[edit] CreateWindowEx(EDIT) failed err=%lu", GetLastError());
        return false;
    }

    int px = -fontPx;
    font_ = CreateFontW(px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, FF_SWISS | VARIABLE_PITCH, L"Segoe UI");
    SendMessageW(edit_, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);

    bg_ = CreateSolidBrush(Theme::fieldBackRGB());
    SetWindowSubclass(edit_, &TextField::EditProc, 1,
                      reinterpret_cast<DWORD_PTR>(this));

    Log::write(L"[edit] field host+EDIT created (Segoe UI %dpx) host=0x%p edit=0x%p "
               L"at screen (%ld,%ld) %dx%d",
               fontPx, (void*)host_, (void*)edit_, sr.left, sr.top, w, h);
    return true;
}

void TextField::setScreenRect(const RECT& sr) {
    if (!host_) return;
    const int w = sr.right - sr.left, h = sr.bottom - sr.top;
    SetWindowPos(host_, HWND_TOPMOST, sr.left, sr.top, w, h, SWP_NOACTIVATE);
    SetWindowRgn(host_, CreateRoundRectRgn(0, 0, w + 1, h + 1, 12, 12), TRUE);
    if (edit_)
        SetWindowPos(edit_, nullptr, editPadX_, editPadY_,
                     w - 2 * editPadX_, h - 2 * editPadY_,
                     SWP_NOZORDER | SWP_NOACTIVATE);
}

void TextField::showAndFocus() {
    if (!host_) return;
    ShowWindow(host_, SW_SHOWNA);
    SetWindowPos(host_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetForegroundWindow(host_);
    SetActiveWindow(host_);
    SetFocus(edit_);
    SendMessageW(edit_, EM_SETSEL, static_cast<WPARAM>(-1), -1);  // deselect, caret at end
    Log::write(L"[edit] field shown; foreground=0x%p focus=0x%p",
               (void*)GetForegroundWindow(), (void*)GetFocus());
}

void TextField::hide() {
    if (host_) ShowWindow(host_, SW_HIDE);
}

void TextField::setText(const std::wstring& s) {
    if (edit_) SetWindowTextW(edit_, s.c_str());
}

std::wstring TextField::text() const {
    if (!edit_) return L"";
    int n = GetWindowTextLengthW(edit_);
    std::wstring s(static_cast<size_t>(n) + 1, L'\0');
    GetWindowTextW(edit_, s.data(), n + 1);
    s.resize(static_cast<size_t>(n));
    return s;
}

LRESULT CALLBACK TextField::HostProc(HWND h, UINT msg, WPARAM w, LPARAM l) {
    if (msg == WM_NCCREATE) {
        auto* cs = reinterpret_cast<CREATESTRUCTW*>(l);
        auto* self = static_cast<TextField*>(cs->lpCreateParams);
        self->host_ = h; // needed before hostHandle() uses host_ during creation
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    auto* self = reinterpret_cast<TextField*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (self) return self->hostHandle(msg, w, l);
    return DefWindowProcW(h, msg, w, l);
}

LRESULT TextField::hostHandle(UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CTLCOLOREDIT: {
        HDC hdc = reinterpret_cast<HDC>(w);
        SetTextColor(hdc, Theme::fieldTextRGB());
        SetBkColor(hdc, Theme::fieldBackRGB());
        SetBkMode(hdc, OPAQUE);
        return reinterpret_cast<LRESULT>(bg_);
    }
    case WM_ERASEBKGND: {
        RECT rc; GetClientRect(host_, &rc);
        FillRect(reinterpret_cast<HDC>(w), &rc, bg_);
        return 1;
    }
    case WM_COMMAND: {
        WORD code = HIWORD(w);
        if (code == EN_CHANGE)   { if (onChange) onChange(); }
        else if (code == EN_SETFOCUS)  { focused_ = true;  if (onFocusChanged) onFocusChanged(true); }
        else if (code == EN_KILLFOCUS) { focused_ = false; if (onFocusChanged) onFocusChanged(false); }
        return 0;
    }
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) {
            Log::write(L"[edit] Esc -> overlay dismiss");
            if (onEscape) onEscape();
            return 0;
        }
        break;
    }
    return DefWindowProcW(host_, msg, w, l);
}

LRESULT CALLBACK TextField::EditProc(HWND h, UINT msg, WPARAM w, LPARAM l,
                                     UINT_PTR, DWORD_PTR ref) {
    auto* self = reinterpret_cast<TextField*>(ref);
    switch (msg) {
    case WM_CHAR:
        if (w == VK_RETURN) {
            Log::write(L"[edit] Enter pressed; text=\"%ls\"", self->text().c_str());
            if (self->onSubmit) self->onSubmit();
            return 0; // swallow -> no MessageBeep
        }
        if (w == VK_ESCAPE) return 0;
        break;
    case WM_KEYDOWN:
        if (w == VK_ESCAPE) {
            SendMessageW(GetParent(h), WM_KEYDOWN, VK_ESCAPE, 0);
            return 0;
        }
        break;
    case WM_NCDESTROY:
        RemoveWindowSubclass(h, &TextField::EditProc, 1);
        break;
    }
    return DefSubclassProc(h, msg, w, l);
}

TextField::~TextField() {
    if (font_) DeleteObject(font_);
    if (bg_)   DeleteObject(bg_);
    if (host_) DestroyWindow(host_);
}
