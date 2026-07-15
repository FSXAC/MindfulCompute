# MindfulCompute — Windows Port Plan

> **For the agent building this on Windows.** This is a design brief, not a
> finished spec. The macOS app in this repo is the reference implementation —
> when in doubt about exact copy, colours, timings, or behaviour, **read the
> Swift source** (paths are cited throughout). Match behaviour, not code.

> Field learnings from actually building this — pitfalls, workarounds, and
> what changed from the plan below — live in `windows-learnings.md`.

## Goal

A Windows build of MindfulCompute with **UX comparable to the macOS app**: a
menu-bar/tray presence, a floating "intention" panel that appears at launch and
on unlock, a full-screen title card with a breathing guide when a session
begins, a live countdown, and a break panel with a reflection prompt that gets
journalled. Target machine: a work laptop (developer **has admin rights**, so
SmartScreen "Run anyway" is an acceptable first-run gate; no code-signing cert
required to start). Development happens on a separate Windows desktop.

## The central insight: the port is *simpler* than the original

Most of the macOS code is a windowing trick, not app logic. AppKit made a
multi-window compositor natural, so the app uses:

- `PanelController.swift` (363 lines) — a borderless `NSPanel` **plus** a
  separate oversized dim-sheet `NSWindow` whose `CALayer`s carve a
  **panel-shaped hole** so the panel's glass samples the bright desktop while
  everything else dims.
- `OverlayController.swift` (160 lines) — a **third** full-screen `NSWindow`
  for the title card, stacked between the dim sheet and the menu bar via window
  levels (`dock+2`, `dock+3`).

**On Windows this all collapses into ONE window:**

> A single full-screen, transparent, always-on-top window that draws the dim,
> the panel, and the title card together, layered by draw order. No
> hole-cutting, no window-level math. (It takes keyboard focus only while the
> intention field is up — see the focus nuance in the stack section.)

Do **not** try to reproduce the dim-sheet-with-a-hole. Draw a semi-opaque dark
layer over the whole screen with a "hole" that is simply the panel element
rendered on top at full opacity. The glass no longer needs to sample the live
desktop (it sits over a dimmed screen anyway) — a semi-translucent fill drawn
in D2D is close enough.

## Multi-display support (new requirement — not in the macOS app)

The macOS app never had to think about a second monitor. Windows does: this is
a fresh requirement, not something ported from Swift. It extends the
single-window model above rather than replacing it — there is still exactly
one "smart" window carrying the UI; every other monitor gets a plain dimmer.

- One "smart" window carries the panel/title card/break panel, placed on the
  monitor containing the cursor at show-time (fallback: primary).
- N "dumb" dim windows, one per remaining monitor: dark, topmost,
  `WS_EX_NOACTIVATE`, eat clicks (each click pulses the panel, matching the
  macOS dimmer behavior), fade on the same choreography. Enumerate via
  `EnumDisplayMonitors`.
- Size every dim window (and the smart window's dim layer) to the monitor's
  **work area**, not full bounds, so the taskbar stays usable on every
  monitor.
- Handle `WM_DISPLAYCHANGE` (and `WM_DPICHANGED`): tear down and rebuild dim
  windows on dock/undock/resolution change — an orphaned dimmer over a
  vanished monitor is the edge case that bites.
- Title card renders only on the smart window's monitor; other monitors just
  deepen their dim.

## Recommended stack: Win32 + Direct2D + DirectWrite + DirectComposition, C++20

- **Stack: Win32 + Direct2D + DirectWrite + DirectComposition, C++20.**
  Rationale: first-party, zero-dependency, sub-1 MB exe, ~10 MB RAM for an
  all-day resident tray app; the same spirit as SwiftUI/AppKit on macOS — no
  framework layer. The app is small and fixed-scope (three screens, ~300-line
  state machine), and Win32/D2D is 20+ years stable with exhaustive
  documentation, which suits agent-driven development.
- **Transparent overlay window: use the modern composition pattern** —
  `WS_EX_NOREDIRECTIONBITMAP` + DirectComposition + a premultiplied-alpha DXGI
  swap chain (D2D renders into it). This is the hardware-accelerated
  per-pixel-transparency path. Do NOT use the legacy `UpdateLayeredWindow` /
  `WS_EX_LAYERED` bitmap path except as a debugging fallback — it fights
  animation.
- Window styles: topmost (`HWND_TOPMOST`), `WS_EX_TOOLWINDOW` (no taskbar
  button, no Alt-Tab), `WS_POPUP`. Note the focus nuance: the window must be
  able to take keyboard focus while the intention field is up (so NOT
  `WS_EX_NOACTIVATE` on the main overlay while input is needed), but must not
  appear in the taskbar/Alt-Tab. Secondary dim windows DO use
  `WS_EX_NOACTIVATE`.
- **The text input field is the single riskiest component.** Phase 0 proved
  the obvious approach doesn't work: a classic Win32 `EDIT` embedded as a
  *child window* of the DComp overlay never composites — DWM presents
  DirectComposition swap-chain content and GDI child windows through separate
  paths, so the EDIT's pixels never reach the screen (it still round-trips
  text via `WM_GETTEXT`, just invisibly). This is fundamental to how DWM
  presents DComp content vs. GDI children, not a side-effect of
  `WS_EX_NOREDIRECTIONBITMAP`. The working pattern (implemented in
  `windows/src/TextField.{h,cpp}`): float the EDIT in a thin, **opaque
  top-level host window** positioned over the panel's field region, not as a
  child of the overlay. The host must be created **unowned** — a
  `WS_EX_NOREDIRECTIONBITMAP` window cannot be an owner, and passing it as
  owner makes `CreateWindowEx` fail with `ERROR_INVALID_WINDOW_HANDLE`. Both
  windows are topmost, with the host in the foreground carrying keyboard
  focus (the overlay itself is shown with `SW_SHOWNA` and never takes
  activation). The host's screen rect must be kept in sync with the panel
  every time the panel moves or animates. Styling that works: `WM_CTLCOLOREDIT`
  + `SetWindowFont`, no `WS_BORDER`, with the host painting its own rounded
  background behind the EDIT. Do not hand-roll a text editor (caret/selection/
  clipboard/IME are a swamp).
- **Phase 0 field notes** — two more gotchas worth carrying into future
  windowed components:
  - In the WndProc thunk pattern, assign the `hwnd` member inside
    `WM_NCCREATE` *before* dispatching to the handler — if the handler calls
    back into `DefWindowProc(nullptr, WM_NCCREATE, ...)` with the hwnd still
    unset, window creation fails.
  - The spike rendered at 96 DPI (1 D2D unit = 1 px); Phase 1 added the real
    handling — geometry and fonts scale by each monitor's effective DPI, and
    `WM_DPICHANGED` now repositions/rescales the overlay, composition target,
    layout, and field host before rebuilding the dimmers.
- **Logging & crash diagnostics (added post-Phase 2, after the first
  user-testing round).** Always-on buffered file logging under
  `%APPDATA%\MindfulCompute\logs\` (5 most recent runs kept, INFO by
  default, `MINDFUL_LOG_VERBOSE=1` adds DEBUG). `SetUnhandledExceptionFilter`
  plus the CRT fatal-path handlers (`abort`/`_purecall`/invalid-parameter)
  catch every crash, write a minidump beside the run log, and flush before
  the process dies. The Phase 0 test watchdog (`MINDFUL_SPIKE_AUTOEXIT`) now
  arms only when that env var is explicitly set — production runs
  indefinitely; the agent testing protocol still sets it on every live run.
  Full detail in `windows-learnings.md`.
- **All animation/color/spacing/timing constants live in ONE header** (e.g.
  `Theme.h`) so tuning passes are cheap. Animations are immediate-mode: a
  frame timer (target 60 fps only while animating, 0 fps when idle — this is
  an all-day app, idle CPU must be ~0%), easing functions, redraw.
- Agent workflow note: after visual changes, verify with screenshots — build a
  habit of capture-and-look since there is no markup hot-reload.
- **Accessibility concession (conscious):** no UI Automation implementation;
  screen readers won't see the panel. Reduced-motion IS honored via
  `SystemParametersInfo(SPI_GETCLIENTAREAANIMATION)`.
- **Sanctioned fallback: WPF/.NET.** Not Tauri/Electron — the owner wants a
  fully native, dependency-free surface, not a WebView2/Chromium wrapper, and
  accepts the slower visual iteration that entails. If the composition window
  or the embedded EDIT turns into a swamp, port to WPF instead — the portable
  core is small enough that the loss is bounded. Do not fall back without
  owner sign-off.
- **Toolchain: MSVC (VS 2022 Build Tools) + Windows 10/11 SDK + CMake +
  Ninja.** Sources live under `windows/` in this repo, e.g.
  `windows/CMakeLists.txt`, `windows/src/`, sharing `assets/` at the repo
  root. Unicode builds (`UNICODE`/`_UNICODE`), per-monitor-v2 DPI awareness
  declared in the manifest.

## macOS API → Windows mapping

| macOS piece (source) | Windows equivalent |
|---|---|
| `MenuBarExtra` live countdown label (`MindfulComputeApp.swift`) | Tray icon **cannot show live text**. `Shell_NotifyIcon` with an `HICON` re-rendered each minute via GDI/D2D into a 16/20/24 px bitmap (DPI-dependent); put `M:SS` in the tooltip. **The one genuine UX compromise.** |
| `NSPanel` + dim sheet w/ hole + title window + `CALayer` | The one-smart-window + N-dim-windows model, layers composed in D2D by draw order |
| Window levels `dock+2/+3` | Draw order within the smart window; `HWND_TOPMOST` for all overlay windows |
| `DistributedNotificationCenter` `com.apple.screenIsUnlocked` (`AppDelegate.swift`) | `WTSRegisterSessionNotification` / `WM_WTSSESSION_CHANGE` (`WTS_SESSION_UNLOCK`) |
| `SMAppService` "Start at login" (`LoginItem.swift`) | `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` value |
| `AVAudioPlayer` bowl (`SoundPlayer.swift`) | `PlaySound(..., SND_FILENAME | SND_ASYNC)`. Reuse `assets/sounds/tibetan_bowl.wav` (also bundled as `Tibetan singing bowl 1.wav` — see Credits), bundled next to the exe or embedded as a resource |
| `NSVisualEffectView` behind-window glass (`Views/PanelRoot.swift`) | Semi-translucent fill + subtle border drawn in D2D (desktop is dimmed anyway; no live blur — same concession as before) |
| `NSWorkspace.open` journal (`Journal.swift`) | `ShellExecute(open)` |
| App Support dir (`Journal.swift`) | `%APPDATA%\MindfulCompute\` via `SHGetKnownFolderPath(FOLDERID_RoamingAppData)` |
| `accessibilityReduceMotion` (`Views/*`) | `SPI_GETCLIENTAREAANIMATION` |
| Build-number stamp `AppVersion` (`MindfulComputeApp.swift`) | Compile-time version + build timestamp constants injected by CMake |

## Portable core to re-express (~300 lines, C++ — do NOT share Swift)

Re-write these natively; they are small and platform-agnostic in spirit. Read
the Swift for exact values.

**`SessionManager.swift` → state machine.** Phases `idle` / `running` /
`resting`. Key behaviours to preserve exactly:
- Countdown is **anchored to wall-clock time** (`endDate = now + minutes*60`,
  `remaining = max(0, endDate - now)`), so lock/sleep never pauses it. Keep this.
- `begin()`: only from `idle` with a non-empty trimmed intention; play bowl;
  1 Hz tick.
- `finish()` (timer hits 0 **or** "End session early"): snapshot the actual
  wall-clock length, pick a random quote, play bowl, go `resting`.
- Break auto-dismiss: after **10 minutes** untouched, journal it as if the user
  clicked Continue and return to `idle` (use a sleep-aware timer, i.e. a
  wall-clock deadline, not a naive countdown).
- Duration slider: **5–60 minutes, step 5**, default **25**.
- Test hooks: `MINDFUL_SECONDS=1` makes the slider count seconds (fast full
  cycle); `MINDFUL_AUTOSTART="text"` begins a session on launch. Reproduce
  equivalents (env vars or a dev flag) — they make the cycle testable in
  seconds instead of minutes.

**`Journal.swift` → persistence.** Write BOTH, to `%APPDATA%\MindfulCompute\`:
- `journal.md` — human-readable, appended. Header `# MindfulCompute Journal`.
  Entry format: `## yyyy-MM-dd HH:mm — N min` (append ` (planned M)` if actual
  ≠ planned), then `**Intention:** …`, then optional `**Reflection:** …`. Use a
  fixed `en_US_POSIX`-equivalent format (invariant culture) so a user's
  locale/clock settings can't mangle it.
- `sessions.json` — structured array of
  `{ start, plannedMinutes, actualMinutes, intention, reflection }`, ISO-8601
  dates. **Preserve the corruption-safety logic**: if the file exists but won't
  parse, move it aside to `sessions.json.corrupt` and start fresh rather than
  clobbering history; if `journal.md` exists but can't be read, skip the append
  rather than overwrite.

**`Quotes.swift` → data port.** Copy the quote list and `{ text, author? }`
shape verbatim.

## UI spec (three screens — reuse exact copy, type, colour)

Read the SwiftUI views for exact wording, spacing, and animation timing. Accent
colours from `Views/PanelRoot.swift`:
`sage = displayP3(0.45, 0.54, 0.42)` (start / setting out),
`ember = displayP3(0.72, 0.55, 0.36)` (break / winding down). Body headings use
a **serif** face; the panel is a 400px-wide rounded card (radius 26).

1. **Intention panel** (`Views/StartView.swift`): time-of-day greeting
   (morning/afternoon/evening), "What are you here to do?", a one-line intention
   field (autofocus ~0.3s after appear), the duration slider with a live
   "For N minutes" label, a sage **Begin** button disabled until the intention
   is non-empty (Enter also submits), version string bottom-right.
2. **Title card** (`Views/TitleCardView.swift`): full-screen over the deepened
   dim. `THE NEXT N MINUTES` (letter-spaced), the intention in large serif
   between two thin rules, and the **breathing guide** below: a swelling
   ring/halo/dot doing one **box-breathing** cycle — 4s in, 4s hold, 4s out —
   with the label cycling "Breathe in" → "Hold" → "Breathe out". Card shows
   ~12s (or ~5s under reduce-motion, static label), then fades; the dim fades
   with it and the desktop returns. Tapping the card skips it.
3. **Break panel** (`Views/BreakView.swift`): ember-accented. `N MINUTES LATER`
   eyebrow, "Time to step away", the intention echoed back, a random quote
   (+ author), a reflection field ("A sentence is enough"), and a **Continue**
   button that journals and returns to `idle`. A small ember dot breathes while
   this page is shown.

**Transition choreography** (from `PanelController.swift` — approximate, don't
obsess): panel appears immediately at launch/unlock while the dim gathers around
it (~2s fade-in). On Begin, the dim closes over the panel's spot as the panel
vanishes (no bright flash), deepens behind the title card, then releases. On
session end, the dim gathers first (~1.2s), *then* the break panel appears —
announced by the dim, not popping in. In the single-window model these are just
opacity/draw-order tweens on shared elements — much simpler than the macOS
handoff.

## Tray behaviour (`MindfulComputeApp.swift`)

- Idle: leaf-style icon; menu item "Bring intention panel to front".
- Running: icon shows remaining **minutes** (rendered into the bitmap); menu
  shows the intention (quoted), "M:SS remaining", "End session early".
- Resting: "Bring break panel to front".
- Always: "Open journal", "Start at login" toggle, version line, "Quit".

## Phased plan (with acceptance criteria)

- **⚠️ Phase 0 — composition + input spike (HARD GATE). ✅ passed
  (2026-07-14, dev desktop, 2 monitors: 4K@150% + 2560×1440@100%).** Prove:
  (a) a DComp transparent, topmost, tool-window overlay that dims the work
  area, eats clicks, doesn't show in taskbar/Alt-Tab, and keyboard types into
  (b) an embedded EDIT control floating on the D2D surface with acceptable
  styling; (c) a tray icon appears and its bitmap can be swapped at runtime;
  (d) a second monitor gets a dim window that blocks clicks and survives
  display-config changes. *Accept:* all four on the dev desktop. If (a) or (b)
  fights for more than ~a day, stop and consult the owner about the WPF
  fallback. All four checks verified live, including a real second-monitor
  dimmer and an injected `WM_DISPLAYCHANGE` rebuild; (b) required the
  top-level-host pattern above rather than a child EDIT. Resulting exe: 337 KB.
  Build with `windows\build.cmd` (vcvars64 → CMake -G Ninja).
- **Phase 1 — UI.** Three screens in D2D/DirectWrite with exact copy, serif
  type (Georgia or Palatino Linotype), sage/ember palette, 400px radius-26
  card, box-breathing guide (4s/4s/4s). *Accept:* click through idle → title
  card → break → idle with a mocked timer; screenshots match the macOS
  spirit. Includes the DPI-scaling work deferred from the spike.
- **Phase 2 — platform wiring.** Unlock re-shows panel; autostart toggle;
  bowl plays; journal read/write with corruption safety; "Open journal"; tray
  countdown; MINDFUL_SECONDS/MINDFUL_AUTOSTART env-var equivalents. *Accept:*
  a fast full cycle writes correct `journal.md` + `sessions.json`.
- **Phase 3 — polish & package.** Fade timings, reduced-motion, idle CPU
  ~0%, single portable exe. *Accept:* runs on the work laptop from a copied
  exe; SmartScreen "Run anyway" is the only gate.

## Reference: macOS source map

| File | Role |
|---|---|
| `MindfulCompute/SessionManager.swift` | **Portable core** — phase machine, wall-clock timer, journal trigger, break auto-dismiss |
| `MindfulCompute/Journal.swift` | **Portable core** — journal.md + sessions.json, corruption safety |
| `MindfulCompute/Quotes.swift` | **Portable core** — quote data |
| `MindfulCompute/Views/StartView.swift` | Intention panel UI/copy |
| `MindfulCompute/Views/TitleCardView.swift` | Title card + breathing guide + box-breathing timing |
| `MindfulCompute/Views/BreakView.swift` | Break panel UI/copy |
| `MindfulCompute/Views/PanelRoot.swift` | Accent colours, panel shape, page-switch logic |
| `MindfulCompute/PanelController.swift` | Transition choreography (reference only — collapses to one window) |
| `MindfulCompute/OverlayController.swift` | Title-card window (reference only — collapses to one window) |
| `MindfulCompute/AppDelegate.swift` | Unlock observer, launch wiring |
| `MindfulCompute/LoginItem.swift` | Start-at-login |
| `MindfulCompute/SoundPlayer.swift` | Bowl playback |
| `MindfulCompute/MindfulComputeApp.swift` | Tray menu + countdown label + version stamp |
