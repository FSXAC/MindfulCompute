# MindfulCompute — Windows Port Plan

> **For the agent building this on Windows.** This is a design brief, not a
> finished spec. The macOS app in this repo is the reference implementation —
> when in doubt about exact copy, colours, timings, or behaviour, **read the
> Swift source** (paths are cited throughout). Match behaviour, not code.

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

> A single full-screen, transparent, always-on-top, **non-activating** window
> that draws the dim, the panel, and the title card together, layered by
> z-index. No child windows, no hole-cutting, no window-level math.

Do **not** try to reproduce the dim-sheet-with-a-hole. Draw a semi-opaque dark
layer over the whole screen with a "hole" that is simply the panel element
rendered on top at full opacity. The glass no longer needs to sample the live
desktop (it sits over a dimmed screen anyway) — a frosted translucent panel via
CSS is close enough.

## Recommended stack: Tauri v2 + web frontend (Electron as fallback)

**Why web-based:** this app's soul is animation — the breathing guide, dim
fades, title-card crossfades, the sage→ember colour shift. CSS / Web Animations
reproduce these ~1:1. Rebuilding them in a native retained-mode UI (WPF/WinUI)
is strictly more work for this particular app.

**Why Tauri over Electron (primary choice):**
- Uses the WebView2 runtime already on Win10/11 → small self-contained `.exe`
  (~5–10 MB), no bundled Chromium. Matters for an all-day tray app.
- v2 supports everything needed: tray, transparent + always-on-top +
  non-activating windows, autostart plugin, and setting the tray icon from
  bytes at runtime (needed for the countdown — see compromise below).

**Electron is the sanctioned fallback.** If Tauri's windowing or tray fights
you, switch — the **frontend HTML/CSS/JS ports over unchanged**. Electron has
more copy-paste answers and a built-in `powerMonitor 'unlock-screen'` event.
Cost: ~100 MB, more RAM. Do not treat the fallback as a rewrite.

**Not recommended:** WPF/WinUI 3 (native). The *only* thing they'd buy is real
Acrylic/Mica glass that samples the live desktop — not worth trading away CSS
animation ergonomics, since the panel is over a dimmed screen regardless.

### ⚠️ Phase 0 is a hard gate — de-risk before building any UI

Before porting a single screen, prove the risky windowing works:

1. A tray icon appears.
2. A full-screen **transparent, always-on-top, non-activating** window that
   dims the screen, **keyboard still types into its input**, the **taskbar
   stays usable**, and it does **not** steal focus / show in the taskbar.
3. You can **swap the tray icon bitmap at runtime** (for the countdown).

If all three work in Tauri → everything else is downhill. If any fights →
switch to Electron *now*, before UI exists. Spend the first half-day here.

## macOS API → Windows mapping

| macOS piece (source) | Windows / web equivalent |
|---|---|
| `MenuBarExtra` live countdown label (`MindfulComputeApp.swift`) | Tray icon **cannot show live text**. Render the remaining **minutes** into the tray icon bitmap each minute; put `M:SS` in the tooltip. **The one genuine UX compromise.** |
| `NSPanel` + dim sheet w/ hole + title window + `CALayer` | ONE transparent, topmost, non-activating window; DOM/CSS layers by z-index |
| Window levels `dock+2/+3` | CSS `z-index` within that window; window itself `alwaysOnTop` |
| `DistributedNotificationCenter` `com.apple.screenIsUnlocked` (`AppDelegate.swift`) | Session-unlock event. Electron: `powerMonitor.on('unlock-screen')`. Tauri: small Rust hook on `WTSRegisterSessionNotification` / `WM_WTSSESSION_CHANGE` (`WTS_SESSION_UNLOCK`) |
| `SMAppService` "Start at login" (`LoginItem.swift`) | Registry `Run` key / Startup folder. Tauri: `tauri-plugin-autostart`. Electron: `app.setLoginItemSettings` |
| `AVAudioPlayer` bowl (`SoundPlayer.swift`) | HTML5 `<audio>` / Web Audio. Reuse `assets/sounds/tibetan_bowl.wav` (also bundled as `Tibetan singing bowl 1.wav` — see Credits) |
| `NSVisualEffectView` behind-window glass (`Views/PanelRoot.swift`) | Frosted panel via CSS (`backdrop-filter` blurs in-page only; the live desktop can't be sampled through a transparent window — acceptable, it's dimmed) |
| `NSWorkspace.open` journal (`Journal.swift`) | Tauri `opener` plugin / Electron `shell.openPath` |
| App Support dir (`Journal.swift`) | `%APPDATA%\MindfulCompute\` (Tauri `app_data_dir` / Electron `app.getPath('userData')`) |
| `accessibilityReduceMotion` (`Views/*`) | `prefers-reduced-motion` media query (Chromium maps it to the Windows "Show animations" setting) |
| Build-number stamp `AppVersion` (`MindfulComputeApp.swift`) | Inject version + build timestamp at build time; show on start panel + tray menu |

## Portable core to re-express (~300 lines, TypeScript — do NOT share Swift)

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
opacity/z-index tweens on shared elements — much simpler than the macOS handoff.

## Tray behaviour (`MindfulComputeApp.swift`)

- Idle: leaf-style icon; menu item "Bring intention panel to front".
- Running: icon shows remaining **minutes** (rendered into the bitmap); menu
  shows the intention (quoted), "M:SS remaining", "End session early".
- Resting: "Bring break panel to front".
- Always: "Open journal", "Start at login" toggle, version line, "Quit".

## Phased plan (with acceptance criteria)

- **Phase 0 — windowing spike (gate).** Tray + transparent/topmost/
  non-activating full-screen dim window that accepts keyboard, leaves the
  taskbar usable, doesn't steal focus, and supports a runtime tray-icon swap.
  *Accept:* all three checks pass in Tauri, else switch to Electron.
- **Phase 1 — UI.** The three screens in HTML/CSS with correct copy, serif
  type, sage/ember palette. *Accept:* can click through idle → title card →
  break → idle by hand (mock the timer).
- **Phase 2 — platform wiring.** Unlock detection re-shows the panel; autostart
  toggle persists; bowl plays on begin/end; journal read/write to `%APPDATA%`;
  "Open journal" opens it; tray countdown renders. *Accept:* a real
  `MINDFUL_SECONDS`-style fast cycle writes a correct `journal.md` +
  `sessions.json` entry.
- **Phase 3 — polish & package.** Match fade timings, honour reduced-motion,
  build a portable `.exe`. *Accept:* runs on the work laptop from a copied exe;
  first-run SmartScreen "Run anyway" is the only gate.

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
