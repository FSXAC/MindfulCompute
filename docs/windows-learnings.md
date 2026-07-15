# MindfulCompute — Windows Port: Field Notes

> **For the project owner and any future agent touching either codebase.**
> `windows-port.md` is the design brief — what to build, and why the
> architecture collapses the way it does. This doc is the debrief: what we
> actually hit while building it, why the workarounds look the way they do,
> and which assumptions from the brief turned out to need revision once code
> existed. Everything here is grounded in `windows/src/*` as it stands after
> Phases 0–2; cite the file, don't take the summary on faith if you're about
> to touch the same code.

## The big one (DComp vs GDI)

### A child EDIT never composites over a DComp surface

This is the single costliest finding of the whole port. The overlay window
(`windows/src/OverlayWindow.cpp`) is `WS_EX_NOREDIRECTIONBITMAP` with a
DirectComposition swap chain (`windows/src/Graphics.cpp`,
`CompositionTarget::init`). A classic Win32 `EDIT` created as a *child* of
that HWND never appears on screen — not "renders wrong," not "renders behind
the panel," genuinely never paints a pixel — even though `WM_GETTEXT` still
round-trips correctly, keystrokes still land in the control, and
`EM_SETSEL`/focus all behave normally. It's a compositor-routing problem, not
a rendering or z-order problem: DWM presents DirectComposition swap-chain
content and GDI child-window surfaces through two separate paths, and a
`WS_EX_NOREDIRECTIONBITMAP` parent never gives GDI a redirection bitmap to
paint the child into. This was diagnosed empirically during Phase 0 (a
magenta solid-color chrome test isolated "control exists and receives input"
from "control's pixels ever hit the screen").

**It is not caused by `WS_EX_NOREDIRECTIONBITMAP` specifically.** Removing
that flag from the parent doesn't fix the child EDIT (the DWM presentation
split runs deeper than that one flag) and it breaks the whole point of the
overlay — the window stops being a hardware-accelerated per-pixel-transparent
surface, so you lose the dim/panel translucency at the same time you gain
nothing.

### The workaround: a floating opaque top-level host

`windows/src/TextField.{h,cpp}` is the whole fix. The EDIT lives inside its
own thin, **opaque**, top-level popup window (`kHostClass =
"MindfulFieldHost"`) that is positioned exactly over the panel's field rect
and kept in sync every time the panel moves, animates, or the DPI changes.
DWM composites top-level windows independently of each other, so a
same-process, ordinary top-level GDI window paints fine even while the
overlay next to it is DComp-composed. Consequences that fell out of this,
each non-obvious in advance:

- **The host must be created unowned.** The natural instinct is to make the
  overlay HWND the host's owner (`hwndOwner` in `CreateWindowEx`) so it
  travels with the overlay's z-order. That fails: a
  `WS_EX_NOREDIRECTIONBITMAP` window cannot be an owner, and passing it as
  one makes `CreateWindowExW` fail with `ERROR_INVALID_WINDOW_HANDLE`.
  `TextField::create` documents this in a comment and literally ignores its
  `owner` parameter (`(void)owner;` in `windows/src/TextField.cpp:36`) —
  both windows are simply kept `WS_EX_TOPMOST` and z-order is managed by
  hand instead of by ownership.
- **The host, not the overlay, carries keyboard focus.** The overlay is
  shown with `SW_SHOWNA` and never takes activation (see
  `OverlayWindow::enterIdle`/`enterBreak`, which call `ShowWindow(...,
  SW_SHOWNA)` then `SetWindowPos(..., SWP_NOACTIVATE)`); the field host is
  brought to the foreground instead
  (`TextField::showAndFocus` — `SetForegroundWindow` +
  `SetActiveWindow` + `SetFocus(edit_)`). macOS's `NSPanel` has a single
  "non-activating panel that still lets one control take key focus" knob;
  Win32 has no single-window equivalent, so the port needed a second window
  to get the same effect.
- **The host rect must track the panel continuously.**
  `TextField::setScreenRect` is called from `OverlayWindow::positionField`
  after every layout pass — on show, on break-panel entry, and on
  `WM_DPICHANGED` — because the host is a separate physical-pixel window
  with no automatic relationship to where the panel is drawn inside the
  DComp surface.
- **Styling is old-school GDI, not D2D.** `WM_CTLCOLOREDIT` sets text/back
  color, `SetWindowFont`/`WM_SETFONT` sets the typeface, `SetWindowRgn` with
  `CreateRoundRectRgn` gives the host rounded corners to match the D2D field
  chrome drawn underneath it in the overlay. No `WS_BORDER`; the host paints
  its own background in `WM_ERASEBKGND` to stay opaque.
- **Don't hand-roll the control.** The brief called this out and it held:
  caret, selection, clipboard, and IME are a swamp not worth entering for a
  one-line field. Reusing the real EDIT control cost one extra window; a
  custom text renderer would have cost weeks.

### WndProc thunk gotcha

Every custom window in this codebase (`OverlayWindow`, `DimWindow`,
`TextField`'s host) uses the same thunk pattern: a static `WndProc` pulls
`this` out of `CREATESTRUCT::lpCreateParams` on `WM_NCCREATE` and stashes it
in `GWLP_USERDATA`. The gotcha, hit once and then defensively copied
everywhere: **assign the `hwnd_` member during `WM_NCCREATE`, before
dispatching to the instance handler**, not after `CreateWindowEx` returns.
If the instance handler runs before `hwnd_` is set and it calls back into
`DefWindowProc(nullptr, WM_NCCREATE, ...)`, window creation fails outright.
See the identical three-line pattern in
`OverlayWindow::WndProc` (`self->hwnd_ = h;` before
`SetWindowLongPtrW`), `DimWindow::WndProc`, and
`TextField::HostProc` (`self->host_ = h; // needed before hostHandle() uses
host_ during creation`).

## Windowing & focus

- Overlay: `WS_EX_TOOLWINDOW | WS_EX_NOREDIRECTIONBITMAP | WS_EX_TOPMOST`,
  `WS_POPUP` (`OverlayWindow::create`). No taskbar button, no Alt-Tab, but it
  *can* take activation when needed — it's `SW_SHOWNA` on purpose so it
  never steals focus from the field host.
- Dumb per-monitor dimmers add `WS_EX_NOACTIVATE` on top
  (`DimWindow::create`) and additionally return `MA_NOACTIVATE` from
  `WM_MOUSEACTIVATE` — belt and suspenders, since a topmost click-eating
  window is exactly the kind of thing that likes to steal focus if you
  don't tell it twice not to.
- The single-window model from the brief held up in practice: one
  `OverlayWindow` (`windows/src/OverlayWindow.h`) draws the dim, the panel,
  and the title card by draw order (`OverlayWindow::render` — dim first,
  then a `switch (screen_)` for content) with a `Screen` state enum that has
  no macOS equivalent (`Idle / TitleEnter / TitleShow / TitleRelease /
  Hidden / BreakGather / Break`) standing in for the three separate NSWindows
  and their level math.
- macOS's non-activating-panel-with-one-focusable-field trick has no
  single-window Win32 analogue — that's *why* the TextField host exists as a
  second top-level window (see above), not just as a rendering workaround.

## DPI

- Declared per-monitor-v2 in `windows/app.manifest`
  (`dpiAwareness = PerMonitorV2`, plus the legacy `dpiAware = true/pm` for
  back-compat). Common Controls v6 is also pulled in via the manifest so the
  themed EDIT control renders correctly.
- D2D drawing happens entirely in logical DIPs: `CompositionTarget::init`
  calls `dc_->SetDpi(dpi, dpi)` so every geometry/font constant in
  `windows/src/Theme.h` is the *same number* at 100% and 150% — D2D scales
  to the swap chain's physical pixels for you.
  `CompositionTarget::dipWidth()/dipHeight()` convert the physical swap-chain
  size back to DIPs (`w_ * 96.f / dpi_`) for layout code.
  `OverlayWindow::toScreen` does the inverse for anything that needs a
  screen-pixel rect (the field hosts): DIP × `scale_` (`dpi_ / 96.f`) +
  window origin.
- The dev desktop actually is mixed-DPI (4K@150% + 1440p@100%, per
  `windows-port.md`'s Phase 0 acceptance note) — this wasn't a hypothetical
  concern, every DPI code path got exercised live by dragging the app
  between the two monitors.
- `WM_DPICHANGED` (`OverlayWindow::handle`, case `WM_DPICHANGED`) does real
  work, not just a dimmer rebuild: it re-queries the monitor's work area and
  effective DPI, repositions/resizes the overlay HWND itself
  (`SetWindowPos(..., work_.left, work_.top, nw, nh, ...)`), resizes and
  re-DPIs the composition target (`comp_.resize` + `comp_.setDpi`),
  recomputes layout, repositions whichever field host is currently visible,
  re-renders, *and then* triggers `onDisplayChange` to rebuild the dumb
  dimmers. `windows-port.md`'s Phase 0 note ("currently that message only
  rebuilds the dimmers") describes the *spike's* state — by Phase 1/2 this
  had grown into the full reposition/rescale path the brief said it would
  need.

## Color

SwiftUI's accents (`Views/PanelRoot.swift`) are specified as Display-P3;
D2D's swap chain here is `DXGI_FORMAT_B8G8R8A8_UNORM`
(`CompositionTarget::init`) with no color-management applied by the OS, so
component values are read as plain sRGB. Reusing the P3 numbers verbatim as
sRGB components would have shifted the palette — P3's gamut is wider than
sRGB's, so the same three floats mean visibly different colors in the two
spaces (roughly: more saturated/vivid if you feed P3 numbers straight into
an sRGB pipe).

`windows/src/Theme.h` carries the actual conversion (linearize via the sRGB
transfer curve → apply the P3→sRGB linear matrix → re-encode with the sRGB
transfer curve), pre-computed to constants with the math documented inline:

```
sage  = Display-P3(0.45, 0.54, 0.42) -> sRGB(0.4265, 0.5434, 0.4080)
ember = Display-P3(0.72, 0.55, 0.36) -> sRGB(0.7518, 0.5413, 0.3263)
```

`Theme::sage()`/`Theme::ember()` return these as `D2D1_COLOR_F`. Everywhere
else in the theme file (dim overlay, panel glass, text tints) is defined
directly in sRGB/white-with-alpha, so this conversion only mattered for the
two brand accents that were ported *from* a P3 source of truth.

One more color-adjacent split worth flagging for future work: D2D drawing
uses `D2D1_COLOR_F` (float, sRGB as above), but the EDIT control and the
tray icon are GDI and need `COLORREF` (`RGB()` macros) — see
`Theme::fieldTextRGB/fieldBackRGB/trayBadgeRGB/trayEmberRGB`. These are
independent, hand-matched approximations of the same colors in a different
representation, not derived from the D2D constants — if the palette changes,
both sets need updating by hand.

## Multi-display

Not a macOS concept at all — the Mac app never had to reason about more than
one screen. The Windows model: one "smart" `OverlayWindow` on the monitor
under the cursor at show-time, plus N "dumb" `DimWindow`s
(`windows/src/DimManager.{h,cpp}`, `windows/src/DimWindow.{h,cpp}`), one per
remaining monitor.

- `DimManager::build` walks `EnumDisplayMonitors`, skips whichever monitor
  is hosting the overlay, and creates a `DimWindow` sized to each other
  monitor's **work area** (`mi.rcWork`, not `mi.rcMonitor`) — the analogous
  concern to "keep the macOS menu bar reachable" is "keep the Windows
  taskbar reachable," and both dumb dimmers and the smart window's own dim
  layer are sized to the work area for exactly that reason.
- `DimManager::apply(level, visible)` drives every dumb dimmer's alpha in
  lock-step with the smart window's `dim_` tween
  (`OverlayWindow::render` calls `onDimChanged((float)dim_.cur, true)` every
  frame it draws), so all monitors fade together.
- Dumb dimmers eat clicks and forward them as a panel pulse:
  `DimWindow::handle`'s `WM_LBUTTONDOWN` case calls `onClick()`, wired in
  `main.cpp` to `overlay.pulse()` — matches the macOS dimmer-click-pulses-
  panel behavior.
- `WM_DISPLAYCHANGE` (and `WM_DPICHANGED`, see above) call
  `DimManager::rebuild`, which is an unconditional `teardown()` +
  `build()` — deliberately simple rather than trying to diff the monitor
  set, specifically to avoid the "orphaned dimmer over a vanished monitor"
  failure mode the brief called out as the edge case that bites on
  dock/undock.
- The title card only ever draws inside `OverlayWindow::drawTitleCard`, so
  it naturally only appears on the smart monitor; other monitors just see
  their dim deepen in lockstep, with no separate "title card" concept to
  keep in sync.

## Glass

`NSVisualEffectView` samples the *live desktop* behind an NSWindow in real
time — genuine backdrop blur. A `WS_EX_NOREDIRECTIONBITMAP` DComp window has
no equivalent API surface; there's nothing in this stack that samples
what's actually behind the window. The accepted concession, called out in
`windows-port.md` and confirmed fine in practice: `Theme::panelFill()` is
just a flat semi-translucent dark fill
(`D2D1::ColorF(0.13, 0.14, 0.17, 0.92)`) plus a 1px
`Theme::panelBorder()` stroke and a subtle top-to-bottom accent gradient
wash (`OverlayWindow::drawCardChrome`). It reads fine specifically *because*
the panel only ever appears over an already-dimmed screen
(`Theme::kDimRest = 0.60`, `kDimDeep = 0.82`) — there's no bright desktop
detail for the fill to need to fake.

The macOS dim-sheet-with-a-hole trick (a second oversized `NSWindow` whose
`CALayer`s carve a panel-shaped hole so the glass could sample the bright
desktop through it) had no reason to exist on Windows once the glass stopped
needing to sample anything: it collapsed entirely into ordinary draw order
inside one window (`OverlayWindow::render`: fill the dim rectangle first,
then draw whichever screen's content on top of it). No hole-cutting, no
window-level math, no second window.

## Tray

`Shell_NotifyIcon`/`NOTIFYICONDATA` cannot show live text the way
`MenuBarExtra`'s label could on macOS — a tray icon is a bitmap, full stop.
`windows/src/TrayIcon.{h,cpp}` works around this by rendering the remaining
**minutes** into an `HICON` at runtime via GDI
(`TrayIcon::renderBadgeIcon`: `CreateCompatibleBitmap` at
`GetSystemMetrics(SM_CXSMICON)`, draw the number with `DrawTextW`, wrap in
`CreateIconIndirect`) and puts the finer-grained `M:SS` countdown in the
tooltip instead (`main.cpp`'s `updateTray` lambda: `_snwprintf_s(tip, ...,
L"MindfulCompute  %d:%02d", ...)`, `TrayIcon::setBadge`). This is called out
in `windows-port.md` as "the one genuine UX compromise" and the code backs
that up — there's no partial win here, just a clean fallback.

A performance detail worth keeping in mind if this file is touched again:
`TrayIcon::setBadge` only re-renders the `HICON` when the integer minute
value actually changes (`lastNumber_` cache, `bool iconChanged = (number !=
lastNumber_)`); a plain per-second tick that stays inside the same minute
only pushes a `NIF_TIP`-only `NIM_MODIFY`, no GDI work. This is deliberate —
it's the mechanism that keeps a 1 Hz running tick from being a steady GDI
cost for an all-day resident app.

Two Windows-specific tray behaviors worth flagging even though they're not
in the source:
- Windows 11 parks *newly added* tray icons in the overflow flyout by
  default on first run — the user has to pin MindfulCompute's icon manually
  the first time, the same first-run gotcha the macOS README already
  documents for menu-bar managers like Bartender/Ice
  (`README.md`'s Notes section). Worth a matching first-run note for Windows
  users.
- `TrayIcon::showMenu` uses `TrackPopupMenu`, which is **modal** — it pumps
  its own message loop until dismissed. `SetForegroundWindow(msgWindow_)`
  before the call and the `PostMessageW(msgWindow_, WM_NULL, 0, 0)`
  immediately after are both there for the standard "menu doesn't dismiss
  correctly on click-away" workaround documented by Microsoft for exactly
  this API.

## Timing & timers

- The countdown is anchored to **wall-clock time**, matching
  `SessionManager.swift`'s `endDate`-based design, and deliberately *not*
  `GetTickCount64` (which pauses across sleep/hibernate — the wrong
  behavior here). `Session.cpp`'s `nowWallMs()` reads
  `GetSystemTimeAsFileTime` and converts FILETIME's 100ns ticks to
  milliseconds; `PageController::begin()` sets `endWallMs_ = startWallMs_ +
  length*1000`, and `remainingSeconds()`/`update()` just compare `nowWallMs()`
  against that deadline. A lock, sleep, or hibernate between `begin()` and
  the deadline never pauses the countdown — it just becomes "already
  elapsed" the next time `update()` polls, exactly like the Mac.
- The break auto-dismiss (10 minutes untouched → journal it as if Continue
  was clicked) uses the same wall-clock-deadline pattern:
  `PageController::startRestTimeout` sets `breakDeadlineMs_` off
  `nowWallMs()`, and `update()` fires `continueFromBreak()` once it's past —
  sleep-aware for the same reason the main countdown is.
- The UI's `TIMER_LOGIC` (`OverlayWindow.cpp`) polls `page_->update()` every
  200ms while a session is running or a break is active — cheap, but it's a
  *poll of a wall-clock deadline*, not a countdown that itself needs to
  survive sleep; the sleep-safety lives entirely in the deadline comparison,
  not in the timer.
- The 60fps frame timer (`TIMER_FRAME`, 16ms) is only armed while something
  is actually animating: `OverlayWindow::animating()` checks the three
  tweens (`dim_`, `card_`, `nudge_`) plus two screen-specific always-animate
  cases (title-card breathing, break-panel ember dot), and
  `stopFramesIfIdle()`/the `WM_TIMER` handler kill the timer the moment
  `animating()` goes false. This is the mechanism behind the brief's "idle
  CPU ~0%" requirement for an all-day resident app — there is no polling
  loop running when nothing is moving.
- The tray's 1 Hz countdown tick is armed/disarmed on phase transitions
  (`onPhaseObserved` in `main.cpp`: `SetTimer(..., TIMER_COUNTDOWN, 1000,
  ...)` only when `p == Phase::Running`), not left running globally — same
  idle-CPU discipline applied to a second, independent timer.

## Data fidelity

`windows/src/Journal.{h,cpp}` is a from-scratch re-implementation (not a
port of Swift code, per the brief's "do NOT share Swift" instruction) built
to be **byte-compatible** with what `Journal.swift` writes, so a user's
existing `journal.md`/`sessions.json` keep working if they run both apps
against the same `%APPDATA%\MindfulCompute\` — sync via a cloud-synced
folder or manual copy was an implicit requirement here.

- `journal.md` entries use local time in a fixed, hand-built
  `yyyy-MM-dd HH:mm` format (`localEntryDate`, built from
  `FileTimeToLocalFileTime` + `swprintf_s` with explicit zero-padding) —
  deliberately not `GetDateFormatEx` with a locale, so a user's regional
  settings can't reformat or mangle the file, matching Swift's
  `en_US_POSIX`-equivalent invariant formatting.
- `sessions.json` timestamps are ISO-8601 UTC with a literal `Z` suffix
  (`isoUtc`), matching Swift `JSONEncoder`'s `.iso8601` strategy, and the
  encoder (`encode()` in `Journal.cpp`) writes keys in **sorted order**
  (`actualMinutes`, `intention`, `plannedMinutes`, `reflection`, `start`)
  with 2-space indentation and a `" : "` separator, matching Swift's
  `[.prettyPrinted, .sortedKeys]` output byte-for-byte in structure.
- Corruption safety is preserved exactly: `appendRecord` in `Journal.cpp`
  tries to parse the existing `sessions.json`; on any parse failure it
  `MoveFileW`s the bad file to `sessions.json.corrupt` (deleting any stale
  `.corrupt` file first) and starts a fresh array rather than clobbering
  history. If `journal.md` exists but can't be read, `Journal::append`
  logs and returns without touching the file — it never overwrites what it
  can't verify it's safely appending to.
- JSON string escaping is hand-rolled (`jsonEscape` in `Journal.cpp`: quotes,
  backslashes, `\b\f\n\r\t`, and `\uXXXX` for other control characters, with
  non-ASCII left as literal UTF-8 to match `JSONEncoder`'s non-`\u`-escaping
  of non-ASCII) rather than pulled from a library, since the project has a
  zero-dependency constraint. This was verified against adversarial inputs
  (intentions/reflections containing quotes, backslashes, newlines,
  surrogate-pair emoji) during Phase 2.
- The JSON *reader* (`Parser`/`Json` structs in `Journal.cpp`) is also
  hand-rolled and intentionally strict: it rejects trailing garbage after
  the top-level array, requires every record to be an object with all five
  typed fields present, and treats anything else as unparseable — which is
  what triggers the corruption-safety path rather than silently accepting
  a partially-valid file.

## MINDFUL_SECONDS semantics

Matched from `SessionManager.swift`'s test hooks, and the Windows
implementation (`Session.cpp`) preserves the exact same trick: only the
*duration unit* scales. `PageController`'s constructor checks for the
`MINDFUL_SECONDS` env var and sets `secondsPerUnit_ = 1.0` (from a default of
`60.0`) — every "minutes" value the slider produces is multiplied by this
factor to get real seconds (`begin()`: `lengthSec = minutes_ *
secondsPerUnit_`). Labels are untouched — the UI still prints "For 25
minutes," "25 MINUTES LATER," etc. — because the label strings read
`minutes_`/`completedUnits_` directly, not the underlying wall-clock
duration.

`PageController::finish()` separately tracks `completedUnits_` (slider units,
for the break-panel eyebrow) and `completedWallMinutes_` (real elapsed
wall-clock minutes, for the journal — `std::llround(elapsedSec / 60.0)`,
independent of `secondsPerUnit_`). Running with `MINDFUL_SECONDS=1` and a
25-unit session therefore takes ~25 real seconds and journals as "1 min
(planned 25)" — exactly the asymmetry described in the task material, and
exactly what `Journal::append`'s `if (actualMinutes != plannedMinutes)`
branch is for.

The break auto-dismiss timeout scales the same way
(`startRestTimeout`: `10.0 * secondsPerUnit_`, unless the separate
`MINDFUL_BREAK_AUTODISMISS_SECONDS` dev override is set), so a fast test
cycle also gets a fast (~10s) auto-dismiss window instead of waiting a real
10 minutes.

## Platform events

| Mac (source) | Windows (source) | Notes |
|---|---|---|
| `DistributedNotificationCenter` `com.apple.screenIsUnlocked` (`AppDelegate.swift`) | `WTSRegisterSessionNotification(overlay.hwnd(), NOTIFY_FOR_THIS_SESSION)` in `main.cpp`, handled in `OverlayWindow::handle`'s `WM_WTSSESSION_CHANGE` case | Extra behavior not in the 1:1 mapping table: unlocking **during a break** calls `bringToFront()` instead of `show()` — the break panel is brought forward rather than the app jumping back to the idle intention screen. Unlocking while a session is `Running` is a no-op (matches `AppDelegate.swift`'s guard). |
| `SMAppService` (`LoginItem.swift`) | `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` (`windows/src/LoginItem.cpp`) | Value name `"MindfulCompute"`, value data is the **quoted** exe path (`quotedExePath()` wraps `GetModuleFileNameW`'s result in literal `"` characters) — unquoted paths with spaces would break on launch. |
| `AVAudioPlayer` (`SoundPlayer.swift`) | `PlaySoundW` (`windows/src/Sound.cpp`) | Two-tier, not a single call: primary path plays the bowl as an embedded `WAVE` resource (`SND_RESOURCE`, `IDR_BOWL`) so the shipped exe stays a single portable file; only if that fails does it fall back to a `tibetan_bowl.wav` sitting next to the exe (`SND_FILENAME`). Both paths use `SND_ASYNC | SND_NODEFAULT` so a missing/broken sound stays silent instead of playing the Windows default ding. |

## Testing an overlay app with agents

A click-eating, topmost, `WS_POPUP` overlay is exactly the kind of thing
that can wedge a desktop if a build ships with a bug — there's no window
manager escape hatch once it's grabbing input. The project treated this as
a first-class risk during agent-driven development, not an afterthought:

- **Mandatory dismissal safety net**, present in every build from Phase 0
  onward: Esc (`OverlayWindow`'s `WM_KEYDOWN`/`VK_ESCAPE` case, and
  `TextField`'s Esc forwarding from the field host up to the overlay), the
  tray's Quit menu item, and a watchdog timer
  (`MINDFUL_SPIKE_AUTOEXIT`, armed **only when that env var is explicitly
  set** — `SetTimer(overlay.hwnd(), TIMER_WATCHDOG, autoexit*1000, ...)` in
  `main.cpp`; see First user-testing round, below, for why an unconditional
  default was a footgun) that force-quits the process if nothing else does.
  The agent testing protocol mandates setting the env var on every
  live/automated run. Three independent exits so a broken build in any one
  of them still can't strand the session.
- **Input injection**: agents drive the overlay via `PostMessage`/`WM_CHAR`
  against the specific HWNDs the app logs at startup — `FindWindow`-by-title
  is unreliable in an automation/harness context because the target window
  may be running on a different desktop station than the driving process,
  so looking the window up by title from outside can silently fail to find
  it. Logging the real HWND values and targeting them directly sidesteps
  that.
- **Verification is screenshot-and-look plus a structured diagnostics log**,
  not just "did the process not crash." `windows/src/Log.h`'s `Log::write`
  is the append-only channel (`spike.log` by default, `MINDFUL_SPIKE_LOG` to
  override) that every window-creation, state-transition, and input event
  writes to — HWND values, extended styles, monitor enumeration
  (`DimManager`'s `enumProc` logs every monitor's bounds/work-area/primary/
  overlay flags), phase transitions, and DPI changes. Separately,
  `MINDFUL_DUMP_DIR` (checked in `OverlayWindow::create` and used via
  `CompositionTarget::requestDump`/`dumpBackBuffer` in `Graphics.cpp`)
  periodically captures the actual DComp back buffer to PNG, composited over
  a neutral gray backdrop so the dim/panel translucency reads correctly —
  this exists specifically because a headless/automation session can't do a
  normal interactive screen capture of what's on screen, so the app has to
  export what it drew itself.
- **Window styles are verified by reading them back**, not just by trusting
  the `CreateWindowEx` call site: `OverlayWindow::create` logs
  `GetWindowLongPtrW(hwnd_, GWL_EXSTYLE)` right after creation
  (`Log::write(L"... EXSTYLE=0x%08llX", ...)`) so a style that silently
  didn't take (Windows will sometimes strip or refuse combinations) shows up
  in the log instead of only being discovered visually.

## First user-testing round (2026-07-15)

Four real bugs came out of the first round of testing on the dev desktop
(the work laptop comes later in the rollout); three debug agents fixed them (`772abbd`, `9481672`, `b01ef07`,
`17c6430`). The shape of the bugs is worth keeping around as a lesson, not
just the fixes.

### Watchdog footgun: a "crash" that was actually a graceful self-quit

The Phase 0 watchdog (`MINDFUL_SPIKE_AUTOEXIT`, see Testing an overlay app
with agents, above) defaulted to 120s and armed **unconditionally** in every
build, including the resident production one — a leftover from the
spike-testing safety net that was never actually gated behind the env var it
was named after. The result: the app quietly `PostQuitMessage`'d itself two
minutes into any real session, which a user testing it reported as "the app
crashed while I was typing my reflection." It wasn't a crash — exit code 0,
clean shutdown, and (this is the tell) **no WER Event 1000** in the Event
Log, because Windows Error Reporting only logs unhandled-exception/fault
exits, not graceful ones. **Lesson for future debugging here: if a user
reports a "crash" and there's no WER Event 1000 for it, stop looking for a
fault and start looking for a self-quit path** (a watchdog, an idle timer,
an explicit `PostQuitMessage`) — the absence of the usual crash signature is
itself the diagnostic signal.

Fixed in `17c6430`: `autoexit` now defaults to `0` (disabled) in
`windows/src/main.cpp`, and `TIMER_WATCHDOG` is only armed when
`MINDFUL_SPIKE_AUTOEXIT` is explicitly set and in range. Production now runs
indefinitely; the agent testing protocol still mandates setting the env var
on every live run — the safety net didn't go away, it just stopped being on
by default for humans.

### Reduced-motion effect gap: correct-by-design, but hit far more often on Windows

The Mac `accessibilityReduceMotion` → Windows `SPI_GETCLIENTAREAANIMATION`
mapping (see What's intentionally NOT ported, below) is mechanically
faithful — same static-vs-animated branch, same trigger. But the Windows
"Animation effects" toggle (Settings > Accessibility > Visual effects) is
off far more often in practice than the Mac equivalent is on: performance
presets, RDP sessions, VMs, and — as it turned out — the dev desktop itself
all commonly have it off. So Windows users hit the static breathing guide at
a much higher rate than Mac users hit reduced-motion, even though both
platforms are equally "correct." It reads as a broken animation, not an
accessibility feature, unless it's diagnosable.

Fixed in `772abbd`: `OverlayWindow::create` (`windows/src/OverlayWindow.cpp`)
now logs a dedicated startup line spelling out the raw
`SPI_GETCLIENTAREAANIMATION` value, whether `MINDFUL_FORCE_MOTION` overrode
it, the effective `reduceMotion_`, and the consequence in plain words
("breathing guide STATIC by design" vs "animated") — also mirrored to
`OutputDebugStringW` so a "why won't it animate?" report is answerable via
DebugView without hunting down the log file. `MINDFUL_FORCE_MOTION=1`/`=0`
still forces either path regardless of the system setting, in every build
config.

### Dev vs release both compile /O2 + NDEBUG

Worth writing down explicitly since it kept coming up while triaging the
above: `windows/build.cmd`'s dev build is `RelWithDebInfo`, release is
`Release`, but both are optimized (`/O2`) with `NDEBUG` defined. There is no
debug-vs-release codegen split here the way there is on many C++ projects —
so "works in dev, not in release" is a red flag pointing at something
*environmental* (a system setting, timing, a machine-specific state), not at
optimizer behavior, and specifically rules out the classic
debug-CRT-zeroes-uninitialized-memory class of bug (dev and release allocate
memory identically here).

### Field host buried by overlay activation

The floating EDIT host (see The workaround: a floating opaque top-level
host, above) shares the overlay's `WS_EX_TOPMOST` band. Any real click on
the overlay activates it, which raises it above the host — so clicking into
the field area a second time landed on the overlay (whose `WM_LBUTTONDOWN`
ignored panel-interior clicks) instead of the buried EDIT, and the caret
never came back. Fixed in `b01ef07`: `OverlayWindow::handle`'s
`WM_LBUTTONDOWN` case now forwards a click inside `fieldRect_`/`reflectRect_`
to the corresponding `TextField::showAndFocus()`, which re-raises the host
and refocuses the EDIT — same as clicking the field on macOS. This only
fires on an actual click; the host still shows without stealing focus on
panel entry, per the original design.

### CreateRoundRectRgn wants physical pixels, not DIPs

`TextField::create`/`setScreenRect` (`windows/src/TextField.cpp`) call
`CreateRoundRectRgn(0, 0, w+1, h+1, rx, ry)` to round the host's corners —
`rx`/`ry` are the corner-ellipse **diameter** (`2 × radius`), and the whole
call operates in the host's native **physical** pixels, since the host is a
physical-pixel top-level window (see Styling is old-school GDI, not D2D,
above). It had been hardcoded to `12`, i.e. a fixed 6px radius, which never
scaled with DPI and drifted out of sync with the D2D chrome's
`kFieldRadius = 11` DIP (16.5px at 150%) — tight, mismatched corners that
made the border look cut off. Fixed in `b01ef07`: the host's radius now
tracks `kFieldRadius * scale_` in physical px
(`OverlayWindow::positionField` calls `f.setCornerRadiusPx(...)` on every
layout pass), and both fields render through one
`OverlayWindow::drawFieldChrome()` helper — a fill-only rounded rect (the
Windows equivalent of macOS's `.quinary` fill) plus, when focused, an accent
ring (sage on start, ember on break) drawn one DIP *outside* the host so it
clears the opaque rectangle and wraps the rounded corners fully. The break
field's old always-on white border (there's no macOS equivalent — both
fields are fill-only, unfocused) was dropped in the same pass.

### DPI gotcha for test harnesses

A DPI-**unaware** driver process posting cross-process positional messages
(`WM_LBUTTONDOWN` with packed `x,y` in `lParam`) gets its coordinates
silently virtualized by the *target* window's per-monitor DPI — a
client-space `1920` arrived at the app as `2880` at 150% scaling, because an
unaware caller is treated as if it's on a 96-DPI virtual desktop and Windows
scales the point for it. Fix on the harness side: make the driver
process-DPI-aware (`PER_MONITOR_AWARE_V2`, matching `windows/app.manifest`),
or precompute and post DIP-space coordinates instead of raw client pixels.
`WM_CHAR` and other non-positional messages are unaffected — this only bites
message types carrying a screen/client coordinate pair.

A second harness gotcha, from testing card dragging (`v1.2.3-win`): once the
app calls `SetCapture` mid-drag, the **real physical cursor** feeds the drag —
synthetic `WM_MOUSEMOVE` posts still arrive, but any actual mouse twitch also
lands, so visual dumps can show the card following the parked hardware mouse
instead of the injected coordinates. That's correct capture semantics, not a
bug; park the physical cursor somewhere deliberate before injecting a drag.

### Logging and crash system (new, `9481672`)

The field crash (typing in the reflection field, during this same round)
left zero diagnostics — no log, no dump, nothing — which is what motivated
all of this:

- **Logging** (`windows/src/Log.h`/`.cpp`): replaced the header-only spike
  logger with a persistent, buffered one. One timestamped file per run under
  `%APPDATA%\MindfulCompute\logs\`, pruned to the 5 most recent runs on
  startup. INFO by default; `MINDFUL_LOG_VERBOSE=1` adds DEBUG. `Log::write`
  kept its exact name/signature so no call site changed. Buffered, flushed
  only on important events (phase transitions, journal writes, startup, and
  always by the crash handler) — idle does zero log I/O, preserving the ~0%
  idle-CPU requirement. `MINDFUL_SPIKE_LOG` still overrides the path for the
  automation harness, unchanged.
- **Crash capture** (`windows/src/CrashDump.h`/`.cpp`):
  `SetUnhandledExceptionFilter` plus `abort`/`_purecall`/invalid-parameter
  handlers all funnel into one place that logs the fault code, address, and
  module+offset, writes a minidump (`.dmp`) beside the run log, flushes,
  then lets the process die. `dbghelp.dll` is `LoadLibrary`'d only at crash
  time — the happy path carries no dependency on it. The CRT fatal paths
  (`abort`, `_purecall`, invalid-parameter) don't hand you an
  `EXCEPTION_POINTERS`, so the handler synthesizes a context via
  `RtlCaptureContext` + `_ReturnAddress()` and guards against re-entrant
  faults while doing it. The handler **must** flush before the process dies
  — `TerminateProcess` (effectively how these paths end) does not flush CRT
  stdio buffers, so an unflushed final log line is silently lost.
- **Verification hook**: `MINDFUL_TEST_CRASH=1` arms a timer that
  null-derefs ~2s after startup, to exercise the whole path (log line →
  flush → dump → die) end-to-end on demand rather than waiting for a real
  crash to test it.
- **Symbolication**: Release now compiles `/Zi` and links `/DEBUG`,
  re-asserting `/OPT:REF /OPT:ICF` (which `/DEBUG` silently disables on its
  own) so the shipped exe stays byte-identical in size; the `.pdb` is a
  separate artifact `build.cmd` copies into `dist/` and is never bundled
  into or required by the exe itself. To symbolize a dump brought back from
  another machine: `cdb -z run-*.dmp -y <pdb dir> -i <exe dir>` then `.ecxr`
  (jump to the exception context) and `k` (stack trace).
- Tray gained an "Open logs folder" item (`ShellExecuteW` on `Log::dir()`).

### PowerShell + a GUI-subsystem exe

Launching `MindfulCompute.exe` from PowerShell with `&` returns immediately
— it's a GUI-subsystem process, so the shell doesn't wait on it the way it
would a console app. Any test/automation script needs an explicit
`Wait-Process` (or equivalent) rather than assuming the launch line blocks.
Related: `MINDFUL_SPIKE_LOG`'s file stays open/locked by the app for the
whole run, so a harness reading it mid-run can hit sharing violations — read
it after the process exits, not while polling it live.

## What's intentionally NOT ported / accepted as different

- **No UI Automation implementation.** Screen readers won't see the panel,
  title card, or break panel — a conscious accessibility concession noted
  in `windows-port.md`, not an oversight. Reduced-motion *is* honored
  (`SystemParametersInfo(SPI_GETCLIENTAREAANIMATION)`, read in
  `OverlayWindow::create` into `reduceMotion_`), so the motion-sensitive
  half of accessibility is covered even though the screen-reader half isn't.
- **No live glass sampling.** The panel's "glass" is a flat translucent D2D
  fill, not a real-time backdrop sample of the desktop — see Glass, above.
- **The tray cannot show live text**, only a re-rendered bitmap plus a
  tooltip — see Tray, above. Called "the one genuine UX compromise" in the
  brief, and nothing in the implementation walks that back.
- **SmartScreen "Run anyway" instead of Gatekeeper ad-hoc signing.** The
  target machine is a work laptop with admin rights and no code-signing
  certificate is in scope for the initial port, so the first-run gate is
  Windows SmartScreen's warning dialog rather than a signed/notarized
  binary — the same category of first-run friction as the Mac's ad-hoc
  Gatekeeper prompt, just a different OS's version of it.
