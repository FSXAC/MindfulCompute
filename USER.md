# MindfulCompute — User Notes

## Things you should know

- **Menu-bar managers hide the timer.** If you use Ice, Bartender, or a
  similar tool, the MindfulCompute item (a leaf when idle, a countdown
  while a session runs) starts out in the hidden section. Unhide it once.
- **Install to /Applications.** The build lands in
  `build/Build/Products/Release/MindfulCompute.app`. Copy it to
  `/Applications` before enabling "Start at login", so the login item
  points at a stable path:

  ```sh
  cp -R build/Build/Products/Release/MindfulCompute.app /Applications/
  ```

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
- **Fast testing mode.** Launch with `MINDFUL_SECONDS=1` in the
  environment and the slider counts seconds instead of minutes:

  ```sh
  MINDFUL_SECONDS=1 /Applications/MindfulCompute.app/Contents/MacOS/MindfulCompute
  ```

## User-driven tests

Run these once after building (use `MINDFUL_SECONDS=1` so a "5 minute"
session lasts 5 seconds). Each line should hold true:

### Start panel
- [ ] Launching the app shows the intention panel, floating above all
      other windows.
- [ ] The panel can be dragged by its background to a new position.
- [ ] The panel stays on top when you click other apps.
- [ ] **Begin** is disabled while the intention field is empty (or only
      spaces), and enables once you type something.
- [ ] The slider moves in 5-minute steps between 5 min and 1 hour, and
      the "For N minutes" label follows it.
- [ ] Pressing Return in the intention field starts the session, same as
      clicking **Begin**.

### During a session
- [ ] The Tibetan bowl sound plays at start.
- [ ] The panel disappears; a countdown (e.g. `4:59`) ticks in the menu
      bar where the leaf icon was.
- [ ] The menu shows your intention, the time remaining, and an
      "End session early" item.
- [ ] "End session early" jumps straight to the break panel (gong included).
- [ ] Lock the screen mid-session, unlock: no panel appears, the
      countdown kept running.

### Session end
- [ ] The gong plays and the break panel appears on its own — even over a
      fullscreen app.
- [ ] The panel shows a quote and "You set out to: …" with your intention.
- [ ] The breathing dot pulses slowly (and holds still if System
      Settings → Accessibility → Motion → Reduce motion is on).
- [ ] After **Continue**, the start panel returns, with intention and
      reflection cleared.

### Data
- [ ] `journal.md` gained an entry with the date, duration, intention,
      and reflection ("Open journal" in the menu shows it).
- [ ] `sessions.json` gained the same session as a JSON object.
- [ ] Ending early records the actual minutes with "(planned N)" noted in
      the journal.

### Unlock behavior
- [ ] With no session running, lock and unlock the Mac: the intention
      panel is waiting for you.
- [ ] Enable "Start at login", log out and back in (or reboot): the app
      is running and the panel appears.
