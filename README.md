<p align="center">
  <img src="docs/icon-256.png" width="128" height="128" alt="MindfulCompute icon">
</p>

<h1 align="center">MindfulCompute</h1>

<p align="center">
  <a href="https://github.com/FSXAC/MindfulCompute/releases/latest">
    <img src="https://img.shields.io/github/v/release/FSXAC/MindfulCompute" alt="Latest release">
  </a>
</p>

A small macOS menu-bar app for intentional computer use, with a native
Windows port (tray icon instead of menu bar — see [Windows](#windows) below).
Before you start working, a floating panel asks what you're here to do and
for how long (5 minutes to an hour). A Tibetan bowl marks the start; the
remaining time counts down in the menu bar. When time is up, a gong sounds
and a break panel invites you to step away — with a quote and a one-line
reflection that's saved to a journal.

## Download

Grab the latest **MindfulCompute.dmg** from the
[Releases page](https://github.com/FSXAC/MindfulCompute/releases/latest),
open it, and drag the app onto **Applications**. On first launch,
right-click the app → **Open** → **Open** (it's free / ad-hoc signed, so
macOS asks once). If it reports "damaged," run:

```sh
xattr -dr com.apple.quarantine /Applications/MindfulCompute.app
```

## Screenshots

<p align="center">
  <img src="docs/screenshots/intention.png" width="420" alt="The intention panel: 'What are you here to do?' with a task field and a 5-minute-to-1-hour duration slider">
  <br>
  <em>The intention panel — set a task and a duration before you begin.</em>
</p>

## How it behaves

- The intention panel appears at launch and every time the Mac unlocks
  (unless a session is already running). It floats above all windows and
  can be dragged anywhere. While it waits, the rest of the screen sits
  behind a dimmer that blocks stray clicks (each one pulses the panel)
  and only lifts once a session begins; the menu bar and keyboard stay
  usable.
- Starting a session plays a Tibetan bowl and shows your intention as a
  full-screen chapter-style title card with a slow breathing guide,
  before the dimmer fades and the desktop returns. The same bowl marks
  the end of the session.
- While a session runs, everything disappears except a countdown in the
  menu bar. The countdown is anchored to wall-clock time, so locking the
  screen or sleeping doesn't pause it.
- The menu bar item offers "End session early", a "Start at login"
  toggle, and a shortcut to the journal.
- Intentions and reflections are appended to
  `~/Library/Application Support/MindfulCompute/journal.md` (readable)
  and `sessions.json` (structured, for revisiting the data later).

See [USER.md](USER.md) for user notes and a manual test checklist.

## Building

Requires Xcode (macOS 26 SDK) and [xcodegen](https://github.com/yonaskolb/XcodeGen):

```sh
./build.sh            # build only
./build.sh --install  # build, replace /Applications copy, relaunch
```

The script stamps a timestamped build number (e.g. `1.1 (260710.1645)`)
shown in the menu-bar menu and on the start panel, so a stale build is
easy to spot. The app lands in
`build/Build/Products/Release/MindfulCompute.app`; keep the installed
copy in `/Applications` so the "Start at login" toggle registers a
stable path.

## Windows

A native Win32 + Direct2D port (C++20) lives in `windows/`, with feature
parity with the Mac app plus multi-monitor dimming (every monitor besides
the one showing the panel just dims).

Requires the VS 2022 Build Tools (C++ workload), CMake, and Ninja:

```sh
windows\build.cmd            # dev build (RelWithDebInfo) in windows\build
windows\build.cmd release    # optimized build -> windows\dist\MindfulCompute.exe
```

The release build is a single portable exe (~2.8 MB, bowl sound embedded as
a resource — nothing else to ship). On first launch, SmartScreen warns about
the unsigned exe — click **More info** → **Run anyway**.

- Data lives in `%APPDATA%\MindfulCompute\journal.md` and `sessions.json` —
  same formats as the Mac app.
- `MINDFUL_SECONDS=1` works the same as on macOS (the duration slider counts
  seconds instead of minutes); `MINDFUL_AUTOSTART="some intention"` begins a
  session immediately on launch.
- "Start at login" is a toggle in the tray menu (it writes the
  `HKCU\Software\Microsoft\Windows\CurrentVersion\Run` key) — the tray
  equivalent of the Mac's menu-bar toggle.
- Windows 11 parks newly added tray icons in the hidden overflow flyout —
  drag the leaf onto the taskbar the first time to keep the countdown
  visible.

See [windows/CHECKLIST.md](windows/CHECKLIST.md) for the manual test
checklist, [docs/windows-port.md](docs/windows-port.md) for the design
brief, and [docs/windows-learnings.md](docs/windows-learnings.md) for
engineering notes from the port.

## Testing the flow quickly

Launch with `MINDFUL_SECONDS=1` in the environment and the duration
slider counts seconds instead of minutes:

```sh
MINDFUL_SECONDS=1 build/Build/Products/Release/MindfulCompute.app/Contents/MacOS/MindfulCompute
```

## Notes

- If you use a menu-bar manager (Ice, Bartender, …), unhide the
  MindfulCompute item the first time — new items are often hidden by
  default.
- Sounds live in `assets/sounds` and are bundled at build time.

## Credits

- Tibetan singing bowl 1.wav by itinerantmonk108 —
  https://freesound.org/s/553049/ — License: Creative Commons 0
