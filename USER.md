# MindfulCompute — User Notes

## Things you should know

- **Menu-bar managers hide the timer.** If you use Ice, Bartender, or a
  similar tool, the MindfulCompute item (a leaf when idle, a countdown
  while a session runs) starts out in the hidden section. Unhide it once.
- **Build with `./build.sh --install`.** It builds, replaces the copy in
  `/Applications` (a stable path for "Start at login"), and relaunches.
- **Check the version to rule out stale builds.** The menu-bar menu and
  the bottom of the start panel show e.g. `1.1 (260710.1645)` — the
  build number is the build's date and time (`yymmdd.HHMM`). If odd
  behavior survives a rebuild, compare this stamp first.

- **"Start at login" matters.** The app can only greet you at unlock if
  it is already running. Enable the toggle in the menu-bar menu.
- **The timer follows the clock, not the app.** Locking the screen or
  sleeping the Mac does not pause a session. If time runs out while the
  Mac is asleep, the break panel greets you when you come back.
- **Where your data lives.** Everything is plain files in
  `~/Library/Application Support/MindfulCompute/`:
  - `journal.md` — readable log of every session (also reachable via
    "Open journal" in the menu).
  - `sessions.json` — the same data as structured JSON
    (`start`, `plannedMinutes`, `actualMinutes`, `intention`,
    `reflection`) for future analysis or tooling.
- **There is no close button on the panel — by design.** The panel stays
  until you commit to a session. It is draggable if it's in the way, and
  quitting the app from the menu always works.
- **The dimmer blocks clicks.** While the intention or break panel is
  up, clicks on the dimmed area go nowhere — each one pulses the panel
  as a reminder of where your attention goes next. Escape hatches: the
  menu bar stays fully usable (end early, quit), Cmd+Tab still switches
  apps, and keyboard input to other apps is not captured — only the
  mouse is gated.
- **The title card can be skipped.** When a session begins, your
  intention takes over the screen for about twelve seconds while the
  breathing guide walks one box-breathing cycle. Click anywhere on it
  to skip ahead. With Reduce Motion on, it shows briefly without the
  pulsing ring.
- **Fast testing mode.** Launch with `MINDFUL_SECONDS=1` in the
  environment and the slider counts seconds instead of minutes. Add
  `MINDFUL_AUTOSTART="some intention"` to begin a session immediately
  on launch (with the default 25-minute duration):

  ```sh
  MINDFUL_SECONDS=1 /Applications/MindfulCompute.app/Contents/MacOS/MindfulCompute
  ```

## User-driven tests

Run these once after building (use `MINDFUL_SECONDS=1` so a "5 minute"
session lasts 5 seconds). Each line should hold true:

### Start panel
- [x] Launching the app shows the intention panel, floating above all
      other windows.
- [x] The panel can be dragged by its background to a new position.
- [x] The panel stays on top when you click other apps.
- [x] **Begin** is disabled while the intention field is empty (or only
      spaces), and enables once you type something.
- [x] The slider moves in 5-minute steps between 5 min and 1 hour, and
      the "For N minutes" label follows it.
- [x] Pressing Return in the intention field starts the session, same as
      clicking **Begin**.
- [ ] The rest of the screen (Dock included) sits behind a dark dimmer
      while the panel is up; the menu bar stays bright.
- [ ] Clicking anywhere on the dimmed area does nothing to the app
      beneath — instead the panel pulses briefly and takes keyboard
      focus.
- [ ] The menu bar still works while the dim is up, and Cmd+Tab still
      switches apps.
- [ ] The panel is glass again — it samples the bright desktop behind
      it, not the dim, and reads clearly brighter than its surroundings.
- [ ] Drag the panel around as fast as you like: the bright region is
      glued to it with zero lag (it moves in the same window-server
      transaction as the panel).

### Session start (title card)
- [x] On **Begin**, the bowl plays and the screen darkens further; your
      intention appears full-screen between two thin rules, with
      "THE NEXT N MINUTES" above it.
- [ ] The breathing ring walks a single box-breathing cycle: 4s swell
      ("Breathe in"), 4s steady ("Hold"), 4s settle ("Breathe out").
- [ ] After about twelve seconds the card fades out, then the dimmer
      fades away, leaving the desktop untouched.
- [x] Clicking anywhere on the title card skips it (card fades, then dim).
- [ ] With Reduce Motion on, the card is shorter and the ring holds still.

### During a session
- [x] The Tibetan bowl sound plays at start.
- [x] The panel disappears; a countdown (e.g. `4:59`) ticks in the menu
      bar where the leaf icon was.
- [x] The menu shows your intention, the time remaining, and an
      "End session early" item.
- [x] "End session early" jumps straight to the break panel (bowl included).
- [x] Lock the screen mid-session, unlock: no panel appears, the
      countdown kept running.

### Session end
- [x] The bowl plays (same sound as the start — the gong is retired) and
      the break panel appears on its own — even over a fullscreen app.
- [x] The dimmer fades back in with the break panel and stays through
      **Continue** and the next start panel, until a session begins.
- [x] The panel shows a quote and "You set out to: …" with your intention.
- [x] The breathing dot pulses slowly (and holds still if System
      Settings → Accessibility → Motion → Reduce motion is on).
- [x] After **Continue**, the start panel returns, with intention and
      reflection cleared.

### Data
- [x] `journal.md` gained an entry with the date, duration, intention,
      and reflection ("Open journal" in the menu shows it).
- [x] `sessions.json` gained the same session as a JSON object.
- [x] Ending early records the actual minutes with "(planned N)" noted in
      the journal.

### Unlock behavior
- [ ] With no session running, lock and unlock the Mac: the intention
      panel is waiting for you.
- [ ] Enable "Start at login", log out and back in (or reboot): the app
      is running and the panel appears.
