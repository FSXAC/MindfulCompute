# MindfulCompute (Windows) — Manual Test Checklist

Everything that a script or a headless build agent **cannot** confirm — it needs
a human at the target laptop, with a real display, real audio, and a real
lock/reboot. Run through this once from the shipped `dist\MindfulCompute.exe`
(copied to the laptop, not run from the build tree). Timings/journal/idle-CPU/
render correctness are already verified automatically; this list is the rest.

Tip: for anything session-related you don't want to wait 25 minutes for, launch
from a terminal with `set MINDFUL_SECONDS=1` first so a "25 minute" session
lasts 25 seconds.

## First launch
- [ ] Double-clicking the exe raises SmartScreen ("Windows protected your PC").
      Click **More info → Run anyway**. It launches. (Expected: no cert, admin
      machine — this is the only first-run gate.)
- [ ] The intention panel appears, centered, floating above everything, with the
      screen dimmed around it. The version line bottom-right reads
      `v1.2.2-win (yyMMdd.HHmm)` and the stamp matches when you built it.
- [ ] The exe has the green leaf icon in Explorer, and the taskbar/tray shows a
      leaf (not a generic window icon).

## Sound
- [ ] On **Begin**, the Tibetan bowl is clearly audible (it plays from inside the
      exe — there is no `.wav` next to it, confirming the embedded resource).
- [ ] The same bowl plays again when the session ends and the break panel appears.

## Lock / unlock (the real thing)
- [ ] With no session running, press **Win+L**, wait a moment, sign back in:
      the intention panel is waiting for you, dim and all.
- [ ] Start a session, **Win+L**, unlock: **no** panel appears and the tray
      countdown reflects the time that passed while locked (the clock kept
      running).

## Tray menu (look + behavior)
- [ ] Right-click the tray leaf: the popup is legible in your Windows theme
      (check **dark mode** especially — text on the menu, no invisible items).
- [ ] Idle menu shows "Bring intention panel to front", "Open journal",
      "Start at login", a greyed version line, "Quit".
- [ ] During a session the tray icon shows the remaining **minutes** as a number,
      the tooltip shows live `M:SS`, and the menu shows the quoted intention +
      "M:SS remaining" + "End session early".
- [ ] **Open journal** opens `journal.md` in your default editor, and it contains
      the sessions you ran.

## Logs & crash diagnostics
- [ ] Tray menu → **Open logs folder** opens `%APPDATA%\MindfulCompute\logs\`
      with a `run-*.log` for this session (5 most recent runs kept).
- [ ] After any crash (or `set MINDFUL_TEST_CRASH=1` before launch to force
      one), the logs folder gains a `run-*.dmp` minidump and the matching
      `run-*.log` ends with a final `[crash]` line (fault code + address +
      module offset) — not a silent cutoff.

## Autostart across a reboot
- [ ] Toggle **Start at login** on. Reboot the laptop. After signing in, the app
      is already running (leaf in the tray) — and if you lock/unlock, the panel
      greets you. Toggle it back off if you don't want it.

## A real, full-length session
- [ ] Run one genuine **25-minute** session start to finish (no `MINDFUL_SECONDS`):
      Begin → title card (~12s, breathing ring does one 4/4/4 box-breath, or a
      still ring if Reduce Motion is on) → work for 25 min → bowl + break panel
      with a quote → type a reflection → **Continue**. The journal gains an entry
      with the date, "25 min", the intention, and your reflection.

## Reduced motion
- [ ] With Windows Settings > Accessibility > Visual effects > **Animation
      effects** ON, the title-card breathing guide animates (swells/holds/
      recedes over the 4/4/4 box-breath). With it OFF, the guide is static
      ("Take a slow breath") — that's correct reduced-motion parity with
      macOS, not a bug. `set MINDFUL_FORCE_MOTION=1` before launch forces the
      animated guide regardless of the system setting, for spot-checking.

## Multi-monitor (if you have a second display)
- [ ] The panel/title-card/break appear on the monitor with your cursor; every
      other monitor just dims (taskbar still reachable). Clicking a dimmed
      monitor pulses the panel.
- [ ] Unplug/replug a monitor (or change resolution) while the dim is up — no
      orphaned dark screen is left behind.
