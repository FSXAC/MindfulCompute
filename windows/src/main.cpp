// MindfulCompute -- Windows port, Phase 1 (UI).
//
// Three screens in Direct2D/DirectWrite (intention panel, title card + breathing
// guide, break panel) over an animated dim, driven by a MOCK session state
// machine (PageController) so a full idle->running->resting->idle cycle runs in
// seconds. The smart overlay carries the UI on the cursor's monitor; dumb dim
// windows follow the same fades on every other monitor.
//
// Safety carried from Phase 0: Esc / tray Quit / MINDFUL_SPIKE_AUTOEXIT watchdog
// dismiss the click-eating overlay in every UI phase.
#include <windows.h>
#include <shellapi.h>
#include <commctrl.h>
#include <shellscalingapi.h>
#include <wtsapi32.h>
#include <string>
#include "Graphics.h"
#include "OverlayWindow.h"
#include "DimManager.h"
#include "TrayIcon.h"
#include "Session.h"
#include "Journal.h"
#include "LoginItem.h"
#include "Log.h"
#include "resource.h"

namespace {
constexpr UINT WM_APP_TRAY        = WM_APP + 1;
constexpr UINT IDM_QUIT           = 0xE001;
constexpr UINT IDM_PANEL          = 0xE002;
constexpr UINT IDM_END_EARLY      = 0xE003;
constexpr UINT IDM_OPEN_JOURNAL   = 0xE004;
constexpr UINT IDM_AUTOSTART      = 0xE005;
constexpr UINT IDM_VERSION        = 0xE006;
constexpr UINT_PTR TIMER_WATCHDOG  = 1;
constexpr UINT_PTR TIMER_COUNTDOWN = 2;

std::wstring exeDir() {
    wchar_t buf[MAX_PATH];
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p(buf);
    size_t slash = p.find_last_of(L"\\/");
    return (slash == std::wstring::npos) ? L"." : p.substr(0, slash);
}
std::wstring envStr(const wchar_t* name) {
    wchar_t buf[512];
    DWORD n = GetEnvironmentVariableW(name, buf, 512);
    return (n > 0 && n < 512) ? std::wstring(buf, n) : std::wstring();
}
} // namespace

int APIENTRY wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    std::wstring logPath = envStr(L"MINDFUL_SPIKE_LOG");
    if (logPath.empty()) logPath = exeDir() + L"\\spike.log";
    Log::init(logPath);
    Log::write(L"[main] MindfulCompute %ls (built %ls)  Phase 2 platform wiring",
               L"" MINDFUL_VERSION, L"" MINDFUL_BUILD_STAMP);

    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);

    int autoexit = 120;
    std::wstring ae = envStr(L"MINDFUL_SPIKE_AUTOEXIT");
    if (!ae.empty()) { int v = _wtoi(ae.c_str()); if (v >= 1 && v <= 3600) autoexit = v; }
    Log::write(L"[main] watchdog autoexit = %d s", autoexit);

    GraphicsDevice gfx;
    if (!gfx.init()) { Log::write(L"[main] FATAL: graphics init failed"); return 2; }

    // Overlay on the cursor's monitor.
    POINT cur; GetCursorPos(&cur);
    HMONITOR overlayMon = MonitorFromPoint(cur, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{ sizeof(mi) };
    GetMonitorInfoW(overlayMon, &mi);
    UINT dpiX = 96, dpiY = 96;
    GetDpiForMonitor(overlayMon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
    Log::write(L"[main] cursor (%ld,%ld); overlay monitor dpi=%u", cur.x, cur.y, dpiX);

    PageController page;
    OverlayWindow  overlay;
    if (!overlay.create(hInst, &gfx, &page, mi.rcWork, dpiX)) {
        Log::write(L"[main] FATAL: overlay create failed"); return 3;
    }

    DimManager dims;
    TrayIcon   tray;

    // App icon (embedded multi-size .ico): the exe's Explorer icon is automatic
    // (first ICON resource); wire it up as the window-class icon and the tray's
    // idle "leaf" base too.
    HICON appIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                      0, 0, LR_DEFAULTSIZE | LR_DEFAULTCOLOR);
    HICON appIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(IDI_APPICON), IMAGE_ICON,
                                        GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON),
                                        LR_DEFAULTCOLOR);
    if (appIcon) {
        SetClassLongPtrW(overlay.hwnd(), GCLP_HICON,   (LONG_PTR)appIcon);
        SetClassLongPtrW(overlay.hwnd(), GCLP_HICONSM, (LONG_PTR)(appIconSm ? appIconSm : appIcon));
    }
    Log::write(L"[main] app icon loaded: large=%p small=%p", (void*)appIcon, (void*)appIconSm);

    auto updateTray = [&]() {
        if (page.phase() == Phase::Running) {
            int rem = page.remainingSeconds();
            int m = (rem + 59) / 60, s = rem % 60;
            wchar_t tip[80];
            _snwprintf_s(tip, _countof(tip), _TRUNCATE, L"MindfulCompute  %d:%02d", rem / 60, s);
            tray.setBadge(m, tip);
        } else if (page.phase() == Phase::Resting) {
            tray.setBadge(-1, L"MindfulCompute  — break");
        } else {
            tray.setBadge(-1, L"MindfulCompute");
        }
    };

    overlay.onQuitRequested = []() { PostQuitMessage(0); };
    overlay.onDisplayChange = [&]() {
        HMONITOR m = MonitorFromWindow(overlay.hwnd(), MONITOR_DEFAULTTONEAREST);
        dims.rebuild(hInst, &gfx, m);
    };
    overlay.onTrayMessage = [&](WPARAM w, LPARAM l) { tray.onCallback(w, l); };
    overlay.onMenuCommand = [&](int id) {
        switch ((UINT)id) {
        case IDM_QUIT:         Log::write(L"[main] tray Quit"); PostQuitMessage(0); break;
        case IDM_PANEL:        overlay.bringToFront(); break;
        case IDM_END_EARLY:    Log::write(L"[main] tray End session early"); page.endEarly(); break;
        case IDM_OPEN_JOURNAL: Journal::open(); break;
        case IDM_AUTOSTART: {
            bool en = LoginItem::isEnabled();
            LoginItem::setEnabled(!en);
            Log::write(L"[main] tray Start-at-login toggled -> %ls", !en ? L"ON" : L"OFF");
            break;
        }
        default: break;
        }
    };
    overlay.onDimChanged = [&](float level, bool visible) { dims.apply(level, visible); };
    overlay.onPhaseObserved = [&](Phase p) {
        updateTray();
        // Idle CPU ~= 0: run the 1 Hz tray tick ONLY while a session is running
        // (it advances the M:SS tooltip; the icon itself only re-renders on a
        // minute change). Idle and resting need no per-second work at all.
        if (p == Phase::Running) SetTimer(overlay.hwnd(), TIMER_COUNTDOWN, 1000, nullptr);
        else                     KillTimer(overlay.hwnd(), TIMER_COUNTDOWN);
    };
    overlay.onTimer = [&](UINT_PTR id) {
        if (id == TIMER_WATCHDOG) { Log::write(L"[main] watchdog fired -> exit"); PostQuitMessage(0); }
        else if (id == TIMER_COUNTDOWN) { updateTray(); }
    };

    dims.onClick = [&]() { overlay.pulse(); };
    dims.build(hInst, &gfx, overlayMon);

    tray.setIdleIcon(appIconSm ? appIconSm : appIcon);   // real leaf for the idle state
    tray.create(hInst, overlay.hwnd(), WM_APP_TRAY, IDM_QUIT);
    tray.onLeftClick = [&]() { overlay.bringToFront(); };
    tray.onBuildMenu = [&](HMENU m) {
        Phase ph = page.phase();
        if (ph == Phase::Running) {
            std::wstring q = L"“" + page.intention() + L"”";   // “intention”
            AppendMenuW(m, MF_STRING | MF_GRAYED | MF_DISABLED, 0, q.c_str());
            int rem = page.remainingSeconds();
            wchar_t buf[64];
            _snwprintf_s(buf, _countof(buf), _TRUNCATE, L"%d:%02d remaining", rem / 60, rem % 60);
            AppendMenuW(m, MF_STRING | MF_GRAYED | MF_DISABLED, 0, buf);
            AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(m, MF_STRING, IDM_END_EARLY, L"End session early");
        } else if (ph == Phase::Resting) {
            AppendMenuW(m, MF_STRING, IDM_PANEL, L"Bring break panel to front");
        } else {
            AppendMenuW(m, MF_STRING, IDM_PANEL, L"Bring intention panel to front");
        }
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(m, MF_STRING, IDM_OPEN_JOURNAL, L"Open journal");
        UINT af = MF_STRING | (LoginItem::isEnabled() ? MF_CHECKED : MF_UNCHECKED);
        AppendMenuW(m, af, IDM_AUTOSTART, L"Start at login");
        AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
        std::wstring ver = L"MindfulCompute " MINDFUL_VERSION L" (" MINDFUL_BUILD_STAMP L")";
        AppendMenuW(m, MF_STRING | MF_GRAYED | MF_DISABLED, IDM_VERSION, ver.c_str());
        AppendMenuW(m, MF_STRING, IDM_QUIT, L"Quit");
    };

    // Greet every unlock (WM_WTSSESSION_CHANGE handled in the overlay proc).
    BOOL wts = WTSRegisterSessionNotification(overlay.hwnd(), NOTIFY_FOR_THIS_SESSION);
    Log::write(L"[main] WTSRegisterSessionNotification -> %ls", wts ? L"OK" : L"FAILED");

    overlay.show();               // idle entrance: panel + dim gathers

    // Test hook: MINDFUL_AUTOSTART="some intention" begins a session on launch
    // with the default duration (pair with MINDFUL_SECONDS=1 for a fast cycle).
    std::wstring autostart = envStr(L"MINDFUL_AUTOSTART");
    if (!autostart.empty()) {
        Log::write(L"[main] MINDFUL_AUTOSTART -> begin(\"%ls\")", autostart.c_str());
        page.setIntentionRaw(autostart);
        page.begin();
    }
    updateTray();

    SetTimer(overlay.hwnd(), TIMER_WATCHDOG, (UINT)autoexit * 1000, nullptr);
    // Countdown tick is started on demand when a session begins (see
    // onPhaseObserved). Only arm it now if MINDFUL_AUTOSTART already began one.
    if (page.phase() == Phase::Running)
        SetTimer(overlay.hwnd(), TIMER_COUNTDOWN, 1000, nullptr);

    Log::write(L"[main] Phase 1 running; message loop entered");
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    KillTimer(overlay.hwnd(), TIMER_WATCHDOG);
    KillTimer(overlay.hwnd(), TIMER_COUNTDOWN);
    WTSUnRegisterSessionNotification(overlay.hwnd());
    tray.destroy();
    dims.teardown();
    CoUninitialize();
    Log::write(L"[main] clean exit");
    return 0;
}
