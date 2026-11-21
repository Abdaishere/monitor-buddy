# Monitor Buddy: maintainer handover

The state of the project at v1.0.0. Where this file and the code disagree, the code is right. Setting a Buddy up as a user (browser installer, then `./setup.sh`) is in the README, "Set up"; this file is for whoever maintains the code. "§" points to a section of this file. Commands run in the project folder unless they say otherwise.

**Contents:** [1 What it is](#1-what-it-is-and-where-everything-lives) · [2 Daily use](#2-daily-use-in-one-screen) · [3 Architecture](#3-architecture) · [4 Build, flash, update](#4-build-flash-and-update) · [5 Tools](#5-every-tool-with-its-command) · [6 Protocols](#6-protocols) · [7 Pages and statuses](#7-pages-and-face-statuses) · [8 Configuration and secrets](#8-configuration-and-secrets) · [9 Security](#9-security-model-and-residual-risks) · [10 Troubleshooting](#10-troubleshooting) · [11 Design decisions](#11-design-decisions) · [12 Limitations and open ideas](#12-known-limitations-and-open-ideas) · [13 Tests](#13-tests)

## 1. What it is and where everything lives

Monitor Buddy is a small screen that sits by your monitor: a Waveshare ESP32-C6-Touch-LCD-1.47 board in a printed holder. It shows an animated face that reacts to the workday, and a round of pages: clock, date, weather, GitHub, Odoo, Claude, pull requests and animations, with the Today and Yesterday pages coming up by themselves. A helper on your Linux laptop (developed on Ubuntu with GNOME) tells it what the laptop is doing (screen lock, microphone, Claude Code sessions, pull requests, Odoo figures, meetings) over an encrypted Wi-Fi link, with USB as the fallback.

The firmware started as the open-source project [IdefixRC/Monitor-Buddy](https://github.com/IdefixRC/Monitor-Buddy) (README badge v1.3.0, MIT licence), whose face comes from DESKBUDDY-1.0 by Edison Science Corner. This fork adds everything else this file describes; `.gitignore` lists what stays on the computer and out of the repository.

### Repository map

| Path | What it holds |
|---|---|
| `src/main.cpp` | The whole firmware, about 6,200 lines: pages, face, input, Wi-Fi link, updates, tasks |
| `src/lunch_face.h` | `LUNCH_FACE_A` and `LUNCH_FACE_B`, the two 320×172 RGB565 frames of the lunch face |
| `src/gh_avatar.h` | `gh_avatar`, the round GitHub avatar (§5, `make_avatar.py`) |
| `setup.sh` | The one command that sets up a Linux computer; it runs `tools/buddy_setup.py` (§4.6) |
| `open-app.sh`, `tools/buddy_app.py`, `tools/app/` | The app: the same setup, and "say", as a page in the browser (§4.8) |
| `ROADMAP.md` | What was left out of 1.0 and why |
| `lib/` | Plain C++ helpers that build both for the board and for PC tests: `persona` (the personal touches: greetings, work days, birthday, reminders; §7.12), `keys` (shape checks of the two keys), `protocol` (USB `@MB` framing), `settings` (the settings struct and a table-driven schema engine), `nightmode`, `display` (number and time-span formats), `odoo` (Odoo page text), `coin`, `github` (streaming scanners), `button` (the BOOT button's press detection) |
| `test/` | Unity tests of `lib/` and their fixtures (§13) |
| `config/config.h` | Build-time settings and secrets (§8) |
| `config/config.h.example` | The template; the build falls back to it when `config.h` is missing |
| `platformio.ini`, `partitions.csv` | Build environments and flash layout (§4) |
| `tools/` | Laptop tools (§5); `tools/chrome-odoo/` is the Chrome extension (§3.5) |
| `docs/HANDOVER.md` | This file |
| `docs/guide/` | The user guide `guide.html`, its 90 screenshots in `shots/`, and the scripts that took them: `capture.py`, and `shots.py` with `steps_day.json` and `steps_game.json`; `technical.html`, the technical reference that `make_pdf.py` adds at the end of `guide.html` when it prints `docs/Monitor-Buddy-Guide.pdf` |
| `docs/images/` | Pictures for the README and the guide |
| `sdcard/` | What goes on the TF card: `gifs/` with the 9 animations drawn by `tools/buddy_art.py` (334 KB), and `README.txt` |
| `web/`, `web-tests/` | The browser installer and settings site (upstream's, adapted to this fork: its links, the key status, the closing summary) and its Node tests. `tools/pages_site.py` assembles the GitHub Pages site from it and the releases (§4.7) |
| `.github/` | This fork's two workflows (`pages.yml`, the installer site; `sim.yml`, the simulator), issue templates, and upstream's workflows parked in `workflows-upstream/`, where they do not run |
| `.pio/` | PlatformIO output; `.pio/build/esp32-c6/firmware.bin` is what gets flashed, `firmware.elf` decodes crash dumps |
| `README.md`, `LICENSE` | This fork's README and MIT licence, with upstream credited |
| `.vscode/`, `.ruff_cache/` | Upstream's editor recommendations, and a linter cache |

### Files outside the repository

| Path | Role |
|---|---|
| `~/.config/systemd/user/buddy-link.service` | Runs the helper `tools/buddy_link.py` (§3.4) |
| `~/.config/fish/conf.d/buddy.fish` | Announces long shell commands on the Buddy (§3.8); sits in the dotfiles repo, untracked |
| `~/.config/google-chrome/NativeMessagingHosts/com.monitor_buddy.odoo.json` | Registers the extension's native host (§3.5) |
| `~/.cache/monitor-buddy/address` | The Buddy's LAN address (§3.4) |
| `~/.cache/monitor-buddy/odoo.json` | The latest Odoo summary (§3.5) |
| `~/.cache/monitor-buddy/claude` | The Claude Code session state (§3.7) |
| `~/.cache/monitor-buddy/sessions/` | One file per open Claude Code session (§3.7) |
| `~/.cache/monitor-buddy/focus` | The focus-sync state (§3.4) |
| `~/.local/share/monitor-buddy/days.csv` | Each work day's first unlock and last lock (§6.3) |
| `~/.config/monitor-buddy/link-key` | The link key, 64 hex digits, mode 600 (§8.4) |
| `~/.config/monitor-buddy/update-key.pem` | The private key that signs firmware updates (§8.4) |
| `~/.config/monitor-buddy/setup.json` | What `./setup.sh` remembers: the answers to its optional questions and which steps were done (§4.6) |
| `~/.local/share/applications/monitor-buddy.desktop` | The app-menu entry `./setup.sh` and the app write (§4.8) |
| `~/.cache/monitor-buddy/app.json` | Where a running app is (pid and address with its token, mode 600), so a second start opens the first |
| `~/.local/bin/buddy-say` | Symlink to `tools/buddy_say.py`, where `./setup.sh` puts it |
| `~/.claude/settings.json` | The Claude Code hooks and status line (§3.7) |
| `~/.cache/evolution/calendar/*/cache.db`, `~/.config/goa-1.0/accounts.conf` | GNOME's copy of the Google calendars, and the account they belong to (§3.6) |
| `journalctl --user -u buddy-link` | The helper's log |

## 2. Daily use in one screen

The Buddy runs by itself: the pages flip on their own and the face switches to a status when something needs attention. What you do with it (the exact gesture rules are in §3.3):

| Do this | What happens |
|---|---|
| Swipe left or right | Next or previous page (§7.1) |
| Swipe up or down | Brighter or dimmer; in the night window it sets the night level (§7.10) |
| Tap | The page's own tap action (§7.2); a showing notice or animation goes away first (§7.8) |
| Tap the face while it says MEETING IN n MIN | The laptop opens that meeting's link in the browser (§7.4) |
| Press BOOT | The laptop locks (§3.3) |
| Double-tap | Automatic page flipping off or on (§7.1) |
| Knock twice on its head | A greeting, then the Today page (§7.4, §7.6) |
| Shake it | Dizzy, a flat stare, then the Today page (§7.4, §7.6) |
| Spin it | A surprised face |
| Lay it flat, stand it up | Starts and stops the focus timer (§7.9) |
| Touch a screen dimmed for the night | It lights up for a while (§7.10) |
| Swipe past the animations page | Bug Hop, the game (§7.11) |
| Hold a finger still on the screen | Opens or closes the Wi-Fi setup hotspot (§3.2) |
| Lock the laptop | The face shows you are away, and touch locks (§7.4, §9.1) |
| `buddy-say "text"` on the laptop | The line shows on the Buddy (§7.8); scripts and Claude sessions can use it too (§3.8) |
| Let a shell command run over a minute | The Buddy announces when it ends (§3.8) |

## 3. Architecture

### 3.1 Overview

```text
   www.odoo.com                GitHub                         Google Calendar
        ^                         ^                                   |
        | JSON-RPC with the       | gh api graphql,                   | GNOME syncs it
        | Chrome session          | every 10 min                      |
+-------|-------------------------|-----------------------------------|---- laptop --+
|  Chrome extension               |                                   |              |
|  "Monitor Buddy: Odoo"          |                                   v              |
|       | native messaging        |                         Evolution Data Server    |
|       v                         |                            calendar cache        |
|  buddy_odoo_host.py             |                                   |              |
|       | odoo.json               v                                   |              |
|       +-----------------> buddy_link.py <---------------------------+              |
|                (service buddy-link, every 5 s, pings twice a second)               |
|                                 ^                                                  |
|                                 |                                                  |
|  lock (logind), mic (PipeWire), Claude Code hook files, usage cache, USB log       |
|                                                                                    |
|  On demand: buddy-say, buddy_flash, buddy_put, buddy_check, buddy_shot             |
+-------------|-----------------------------------------------|----------------------+
              | UDP 4747, sealed with LINK_KEY                | USB serial: fallback,
              | STATE, Odoo, notices, updates,                | settings, rescue; its log
              | animations, screenshots                       | gives the IP address
              v                                               v
+-------------|--------- Monitor Buddy (ESP32-C6) ------------|----------------------+
|  loop(): USB commands, link, touch, sensors, statuses, pages, screen (30 fps)      |
|  netTask: weather, GitHub, stock, coin, NTP  ---------------->  internet           |
|  knockTask: motion sensor every 3 ms                                               |
|  Flash: two firmware slots, settings, Wi-Fi, day totals, animations; TF card       |
+------------------------------------------------------------------------------------+
```

Everything about the laptop and the work comes from `buddy_link.py`, which polls local sources and sends packets to the Buddy; the rest the Buddy fetches itself (§3.9). The Buddy never opens a connection to the laptop; it only answers. The on-demand tools use the same link (§6).

### 3.2 The board

**Hardware.** An ESP32-C6FH8 with 8 MB of flash and no PSRAM; a 320×172 panel (JD9853 controller) driven through Arduino_GFX's `Arduino_ST7789` class plus a custom register batch, `lcdRegInit()`; an AXS5106L touch controller at I2C address 0x63, polled; a QMI8658 motion sensor at 0x6B (±4 g, ±512 °/s); a TF card slot on the display's SPI bus; an ETA6098 charger with no battery fitted. The USB-C port is the chip's own USB-Serial-JTAG: the serial console (115200 baud) and the flashing port. The `mount` setting turns the screen for a left mount (rotation 1) or a right mount (rotation 3, with the tilt sign flipped to match).

| GPIO | Use |
|---|---|
| 1, 2 | SPI clock and data out, shared by the LCD and the TF card |
| 3 | TF card data in |
| 4 | TF card chip select; driven high first thing at boot |
| 14, 15, 22 | LCD chip select, data/command, reset |
| 23 | Backlight PWM, 5 kHz, 8 bit; `writeBacklight()` is its only writer |
| 18, 19 | I2C data and clock at 400 kHz, shared by touch and motion sensor |
| 20 | Touch reset: low for 200 ms, then a 300 ms wait |
| 21 | Touch interrupt, pulled up but unused |
| 5, 6 | Motion-sensor interrupts, unused |
| 7 | AyresWiFiManager's LED and button pin: a free pin, so the library never drives GPIO2 or reads GPIO0 |
| 8 | Strapping pin and the on-board RGB LED |
| 9 | BOOT button, a strapping pin, read with a pull-up: a press asks the laptop to lock (§3.3) |
| 12, 13 | USB-Serial-JTAG |

**Start-up, `setup()`.** In order: serial (receive buffer 4 KB, send buffer 2 KB; output nobody reads is dropped); LittleFS (formatted if it will not mount) and the settings (§8.1); the display (SPI at 40 MHz) and the backlight; I2C, touch and the motion sensor; "HOLD STILL" while the rest position is measured (a 900 ms wait, 100 discarded readings, then 140 readings 5 ms apart); the knock task; the page lists; the clock, seeded from the build time; AyresWiFiManager; the choice between the two saved networks; the animation source (§7.7); the day totals; the brightness from `/display.json`; removal of upstream's old `/index.html`, `/success.html` and `/error.html`; the Wi-Fi join (a 2 s button window, then a 15 s attempt); NTP; `netTask`. USB `@MB` commands get no answer until `setup()` has finished.

**Tasks and shared data.**
- `loop()` does everything on screen. Each pass: USB `@MB` commands; the setup hotspot when it is open (below); Wi-Fi upkeep every 500 ms; the link; the sensors every 70 ms (not during a Bug Hop run); touch, the BOOT button and pending brightness saves; knock events; page, timer, night and status updates; NTP; drawing; sending changed rows to the panel; the status line every 1.2 s; then a wait so that a pass takes at least 33 ms (`FRAME_MS`, at most 30 frames a second; no wait during a Bug Hop run).
- `netTask` ("net", 12 KB stack, priority 1) fetches at most one thing per pass, in the order coin, stock, weather, GitHub. It sleeps 250 ms after a fetch, 500 ms otherwise and 1 s while Wi-Fi is down, and it starts NTP. After a failed fetch `FetchPacer` waits 10 s, doubling up to 60 s (120 s for GitHub). It writes its results into `netShared` (`publishNetData()`); the loop writes only `netShared.odoo` (`odooFromPc()` and the stale-data drop) and copies `netShared` to `netView` once a frame (`snapshotNetData()`), all under `netMutex`.
- `knockTask` ("knock", priority 2) is the only reader of the motion sensor (§3.3); `readSensors()` reads it itself only when the knock task did not start. Every 23rd tick, about every 70 ms, it takes the full reading the loop uses, which the loop copies inside the `imuMux` critical section. Touch reads and the knock task take turns on the I2C bus under `i2cMutex`; `resetSharedI2CBus()`, run after a touch read error at most every 250 ms, holds it too.

**Screen.** Everything draws into one 320×172 RGB565 canvas. `flushChanged()` hashes each row (FNV-1a over 160 32-bit words) and sends only the runs of changed rows. The whole frame goes out every 5 s and whenever `flushForced` is set (boot messages, the end of a Bug Hop run); it takes about 30 ms. During a Bug Hop run `gameFlush()` sends instead the play band (rows 76–151) every frame, the hop box (x 36–77, rows 18–75) while the runner is in the air and one frame after, and one of six slices of the rest, so every row goes out at least every sixth frame.

**Eyes.** The springs (`faceStep()`; eye k 0.12 and damping 0.60, pupil 0.08 and 0.50) step every 70 ms (`FACE_STEP_MS`), and the frames in between show a linear blend (`eyeBetween()`). After more than a second without a step the state resets instead of catching up. The springs run only while the face page draws eyes. Breathing is sin(t/800)×3.5 px; a blink closes the eyes to 4 px for 120 ms every 2–6 s; a saccade every 0.5–3 s picks the centre 40 % of the time, else one of six offsets; the pupils follow the tilt by (20, 12) px and the eye centres by (8, 6). `updateFaceTimers()` still writes `blinkUntil`, `faceGlanceX/Y` and `pressPulse`, which nothing reads.

**Wi-Fi, setup hotspot and clock.**
- AyresWiFiManager (IdefixRC fork 2.5.0), host name `Monitor-Buddy`, fallback policy `NO_CREDENTIALS_ONLY`: the setup hotspot opens by itself only when no network is saved. `maintainWifi()` retries a saved network that fails: a 15 s attempt, then a 60 s pause.
- Two saved networks, `/wifi.json` and `/wifi2.json`. At boot only, `chooseSavedWifi()` scans when both are set; if the first is out of range and the second is in range, it swaps the two files, through `/wifi.swap`, which the next boot recovers if the swap was cut short.
- The setup hotspot: network `AP_SSID` (default `Monitor-Buddy-Setup`) with the WPA2 password `AP_PASS`, a captive DNS, the address http://192.168.4.1, and AyresWiFiManager's pages in English, Spanish and German. Its screen shows the network name, the password, the address, the number of connected clients and a blinking dot. The hold of §3.3 opens it; lifting the finger and holding again closes it and reconnects to the saved network. It closes by itself after 300 s without a connected phone or an HTTP request. While it is open the loop serves only USB `@MB` commands and the hotspot (with an 8 ms pause), the backlight stays at the day level, and network fetches refuse. Its erase button deletes `/wifi.json` and `/display.json`.
- NTP: `startNtp()` on `netTask` looks up pool.ntp.org and time.cloudflare.com itself and gives SNTP addresses, never host names (§11.1). The loop asks for a sync every 60 s until one completes with a year after 2000, and again 6 h after the last one. The first success sets `clockTrusted` for good. Until then the clock runs from the build time, and night mode, the lunch face, meeting warnings, the hours nudge, the Today triggers, the day roll-over and the coin chart wait for it. A code comment says the system clock survives software restarts and flashing; a power cut loses it.

**Rules for changing the firmware.**
- Compare `millis()` stamps only with `millisSince()`, `secondsSince()` or `timeReached()`. Stamps are stored as `millis() | 1`, so that 0 means "unset", which can put a stamp 1 ms in the future; a plain `millis() - stamp` then underflows to about 4e9 (§11.1).
- Call `setTextSize(1)` before u8g2 text and `setFont(nullptr)` after it, or the next pages inherit the font and size. u8g2 fonts work only because `olikraus/U8g2` is in `lib_deps`.
- Keep the task ownership of the bullets above: who writes `netShared`, who reads the motion sensor, and `i2cMutex` around every bus access.
- ArduinoJson stays on v7.

### 3.3 Input detection

Touch, polled by `readTouch()`:

| Gesture | Rule |
|---|---|
| Tap | Down for 35–650 ms, never moved more than 12 px, and under 35 px between press and release on both axes. A release counts after 3 missed polls and at least 120 ms since the panel last reported the finger |
| Double tap | A second tap within 400 ms; the first tap has already done its action |
| Horizontal swipe | More than 55 px sideways and more than the vertical move plus 18 px; one page per press |
| Vertical swipe | More than 55 px up or down and more than the sideways move plus 18 px; one brightness step of 24 (range 8–255) per 55 px of drag |
| Hold | Still for more than 2500 ms (`TOUCH_PORTAL_HOLD_MS`) on a screen that is not dimmed |
| Press on a dimmed screen | At least 35 ms and not a swipe; every press renews the wake time. A sideways swipe there only wakes the screen |

The BOOT button, read by `readBootButton()` on every pass of the loop (`ButtonEdge` in `lib/button`): a press counts once, when the pin goes low. A change within 300 ms of the last one taken is ignored, and a button that reads pressed at the first read counts only after a release. A press sets the lock request of §6.1, which `buddy_link` carries out (§3.4); it travels only over Wi-Fi.

Motion, read by `readSensors()` every 70 ms and smoothed (acceleration 0.88 old plus 0.12 new; rotation 0.82 plus 0.18), relative to the rest position measured at boot:

| Event | Rule |
|---|---|
| Shake | A jolt is a change of more than 0.9 g in the total pull between two readings. A shake starts when five jolts fall within 800 ms; every further jolt extends it |
| Spin | Rotation about the screen's axis faster than 130 °/s |
| Lie flat | More than 0.8 g through the board for more than 2 s |
| Tilt | The eyes follow the tilt (gain 2.2, clamped); without a motion sensor they sway on a sine |

The knock detector, `knockTask`, reads the acceleration every 3 ms (`KNOCK_EVERY_MS`):
1. The jolt is the length of the difference between the acceleration and a slow average, on all three axes; the average moves 0.02 of the way toward each sample. The sample of every 23rd tick (the full reading for the loop) is not checked.
2. A jolt above 6.0 g (`KNOCK_MAX_G`) means the Buddy is being handled: the pair resets, and no double knock counts for the next second.
3. A knock is a jolt from 0.25 g (`KNOCK_G`) to 6.0 g after at least 50 ms (`KNOCK_STILL_MS`) of stillness, meaning no jolt of 0.08 g (`KNOCK_STILL_G`) or more.
4. The first knock also needs 150 ms (`KNOCK_LEAD_MS`) of stillness before it, which typing never gives. Without a second knock within 700 ms (`KNOCK_GAP_MAX_MS`) it is dropped.
5. A later jolt under 25 % (`KNOCK_ECHO`) of the first knock is the Buddy rocking on its stand and is ignored. The second knock comes at least 80 ms (`KNOCK_GAP_MIN_MS`) after the first; a later knock under 60 % of the first moves it forward.
6. A third knock of 60 % of the first or more cancels the pair (typing or handling).
7. When 250 ms (`KNOCK_QUIET_MS`) pass after the second knock, the pair counts as a double knock, unless a finger was on the screen from 300 ms before the first knock, the Buddy was handled within the last second, or a shake is being answered.

Every knock makes the eyes blink; §7.4 has what a double knock does. Tune only from data: `last_knocks_g` in the health answer (§6.5) and `knock=` in the status line (§6.7).

### 3.4 The laptop helper, `tools/buddy_link.py`

The systemd user service `buddy-link` runs it:

```ini
[Service]
ExecStart=/usr/bin/sg dialout -c "/usr/bin/python3 /home/YOU/Monitor-Buddy/tools/buddy_link.py"
Restart=always
RestartSec=10

[Install]
WantedBy=default.target
```

`sg dialout` is there for a login session that started before the account joined the `dialout` group, which grants access to the serial port. The service holds that port while it runs. `/usr/bin/python3` provides `cryptography` and `python-dateutil` for the tools, Pillow for `buddy_shot.py`, `buddy_gif.py` and `buddy_art.py`, and pyserial for `mb_serial.py`.

Every 5 s (`POLL_S`) it:
1. Fetches pull requests on the first pass, every 10 min, and when the date changes, with `gh api graphql` (30 s timeout): your open pull requests (first 50) with number, title, last update, review decision and the check rollup of the last commit; the number of open pull requests that ask for your review; and two counts for the working day before (Friday on a Saturday, a Sunday or a Monday), from 00:00:00 to 23:59:59 local time: `is:pr author:@me is:merged merged:DAY` and `is:pr reviewed-by:@me -author:@me updated:DAY`. A failed call keeps the previous figures.
2. Reads the screen lock: `loginctl show-user UID -p Display --value`, then `loginctl show-session SESSION -p LockedHint --value` must say `yes`. A hung `loginctl` keeps the last answer.
3. Reads the microphone with `pw-dump`: something records when a node has `media.class` `Stream/Input/Audio`, state `running`, a `media.name` other than `Peak detect`, and no `stream.monitor`.
4. Reads the Claude file, the session files and the usage cache (§3.7).
5. Drains the Buddy's USB log when the Buddy is plugged in (below).
6. Sends a STATE (§6.3) when it changed or 60 s have passed, else a ping `P`: Wi-Fi first (one try, 1 s), then `@MB STATE` over USB; with neither route it tries again on the next pass. The journal shows `sent wifi {...}` or `sent usb {...}` only when the state or the route changes.
7. Notes whether the Buddy's timer runs, from bit 0 of the link answer or from `timer=` in the USB status line. With no sign of the Buddy for 60 s the timer counts as stopped. It acts on bits 2 and 3 of the answer (below).
8. Sends the Odoo packet (§3.5).
9. Runs focus sync.
10. Until 5 s after the round began, pings the Buddy every 0.5 s (`PING_S`) and acts on the bits of each answer.

What the Buddy asks for, in the status byte of its answer to a `P` or a STATE (`act()`):
- Bit 2, join: the helper picks, among the meetings of the last Odoo packet with their links (§3.5), the first that starts within 10 minutes (`JOIN_BEFORE_S`) or began under 90 s ago, else one under way; of two with the same start, the one with a link. It runs `xdg-open` on the link when it starts with `https://`, which opens the default browser, and logs `join: NAME`; otherwise `join: NAME has no https link` or `join: no meeting within 10 minutes`.
- Bit 3, lock: `loginctl lock-session` on the session that `loginctl show-user` names as Display, logged as `lock: the BOOT button`. The helper never unlocks.
- A failure of either logs `could not do what the Buddy asked: …`.

Focus sync follows the Buddy's timer:
- When the timer starts, it saves `show-banners` of `org.gnome.desktop.notifications` and the Caffeine extension's `cli-toggle` (its schemas are in `~/.local/share/gnome-shell/extensions/caffeine@patapon.info/schemas`) to `~/.cache/monitor-buddy/focus`, turns banners off and logs `focus on`.
- While it runs, Caffeine stays on as long as the session has been idle for under 30 min (Mutter's `IdleMonitor`) and goes off after that, so an unattended laptop still locks. Caffeine is left alone if it was already on.
- When the timer stops, or the Buddy has been out of reach for 60 s, Caffeine goes off (unless it was on before), banners return to their saved value, the focus file is deleted and the journal says `focus off`.
- On SIGTERM or a normal exit an `atexit` hook restores the settings. After a crash the next start finds the focus file and restores them once the timer is off. Errors are logged as `focus sync failed: …`.

USB:
- The port is the command-line argument, else the first `/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_*`, looked up again at every reopen. It is opened raw and non-blocking with HUPCL cleared, so closing it does not restart the Buddy.
- Every pass drains the log and keeps the last 8 KiB. Lines that start with `mood:` or `knock:` go to the journal as `buddy …`. The status line gives `timer=` and `ip=`; a new address other than `0.0.0.0` is written to `~/.cache/monitor-buddy/address` (journal: `buddy address: …`). A read of zero bytes means the Buddy was unplugged (`lost the Buddy: …`); the next pass opens the port again (`connected`).
- The link key and the address file are read once at start, so restart the service after changing either by hand (`./setup.sh` does that itself). The key comes from `~/.config/monitor-buddy/link-key`; without that file, from `LINK_KEY` in `config/config.h` (`buddy_link.link_key()`).

### 3.5 Odoo through Chrome

The extension in `tools/chrome-odoo/` (Manifest V3, "Monitor Buddy", version 1.3 on disk; since 1.3 its toolbar button opens `popup.html`, which sends `{"say": text}` to the native host, and the host shows it with `buddy_say.say()` instead of saving it) is loaded unpacked in Chrome's `Default` profile. The `key` in its `manifest.json` pins its ID to `hpfaofllppcbcopbdbanihdnfnbkcgad`. Permissions: `alarms` and `nativeMessaging`; host permission `https://www.odoo.com/*` only.

`worker.js` runs a tick on the alarm `odoo` (every minute; it re-creates the alarm when the period is not one minute), on Chrome start-up, and when the extension is installed or reloaded. A tick posts JSON-RPC requests to www.odoo.com with `credentials: 'include'`, so they use the session Chrome is signed in with; there is no API key. Every request reads:

| Key | What it is |
|---|---|
| (session) | `/web/session/get_session_info`. Without a `uid` the result is `{"error": "signed out of www.odoo.com"}`. The partner comes from `partner_id`, else from `res.users.read` |
| `h` | Today's hours: the sum of `unit_amount` of your `account.analytic.line` records for today's local date |
| `hb`, `bw` | The same for the previous working day (Monday looks back to Friday, Sunday to Friday, other days to yesterday), and that day's weekday, 0 = Sunday |
| `y` | The names (40 characters) of your `project.task` records whose `write_date` falls on that day in local time, any stage, the 3 changed last |
| `o` | Open tasks: `project.task` with you among `user_ids` and a stage whose name contains none of done, cancel, parking and template |
| `r` | Open tasks whose stage name contains "code review" |
| `d` | Open tasks whose stage name contains "development" |
| `a` | Your `mail.activity` records due today or earlier |
| `t` | The 6 open tasks changed last, as `[id, name (64 characters), stage (40 characters)]` |
| `m` | Your `calendar.event` records from now to 24 h ahead, not all-day, not declined, the first 3, as `[name (40 characters), start, stop, link]`, times in UTC seconds, the link being `videocall_location` or `''`. An Odoo whose `calendar.event` has no `videocall_location` fails that read, which then runs again without the field |

That is 10 requests per tick, one more when the session lacks `partner_id` and one more when the meeting read is retried. A thrown error becomes `{"error": message}`. The result goes to `chrome.runtime.sendNativeMessage('com.monitor_buddy.odoo', …)`; when the host fails, only a `console.warn` in the worker shows it.

Chrome starts the native host `tools/buddy_odoo_host.py` once per message, as `~/.config/google-chrome/NativeMessagingHosts/com.monitor_buddy.odoo.json` registers it:

```json
{
  "name": "com.monitor_buddy.odoo",
  "description": "Monitor Buddy: saves the Odoo summary from the Monitor Buddy extension",
  "path": "/home/YOU/Monitor-Buddy/tools/buddy_odoo_host.py",
  "type": "stdio",
  "allowed_origins": [
    "chrome-extension://hpfaofllppcbcopbdbanihdnfnbkcgad/"
  ]
}
```

The host reads a 4-byte little-endian length and the JSON message. It stops with "message too big" above 64 KiB and "not an object" for anything but a JSON object. It writes `odoo.json.part` with mode 600, renames it over `~/.cache/monitor-buddy/odoo.json` (creating the folder with mode 700 when it is missing) and answers `{"ok":true}`. Error objects are saved like data.

`buddy_link` sends nothing, and logs why, when the file is older than 30 min ("no news from Chrome for 30 minutes"), missing or unreadable ("nothing from Chrome yet"), or holds an `error` (its message, such as "signed out of www.odoo.com"). Otherwise it adds the Google meetings (§3.6) to `m`, merges duplicates with the same name and start (keeping a link either one has), sorts by start, keeps that list with the links for joining (§3.4), and sends the first 3 as `[name, start, stop]`: the links stay on the laptop. It writes the result as compact JSON and, while that is over 1300 bytes, drops the last entry of `t`, then of `y`, then of `m`. It sends `O` with the JSON when it changed or 60 s have passed: Wi-Fi with one try, else `@MB ODOO` over USB. The journal says `odoo: REASON` or `odoo: data from Chrome` whenever that changes.

### 3.6 Google Calendar

`tools/buddy_calendar.py` reads the caches that GNOME's Evolution Data Server keeps for the Google account in GNOME Online Accounts: `~/.cache/evolution/calendar/*/cache.db`, opened read only (`mode=ro`, 2 s timeout) with `select ECacheOBJ from ECacheObjects`. It uses no network and no Google credentials. Your addresses are the `Identity=` lines of `~/.config/goa-1.0/accounts.conf`.

- It returns `[name, start, stop, link]`, times in UTC seconds, sorted by start, for the occurrences that end after now and start within the next 24 hours (or `HOURS`), with duplicates on name and start removed.
- The link is the event's `X-GOOGLE-CONFERENCE` when that starts with `https://`, else the first `https://` URL on meet.google.com, zoom.us, teams.microsoft.com or teams.live.com (or a subdomain) in LOCATION, DESCRIPTION or URL, cut at a space, a quote, `<`, `>` or a backslash, with `&amp;` turned into `&`; else `''`.
- It skips events without DTSTART, with a STATUS line containing CANCELLED, with an ATTENDEE line that has `PARTSTAT=DECLINED` and one of your addresses, and whole-day events.
- No DTEND means one hour. A time ending in `Z` is UTC; a `TZID` goes through zoneinfo, and a name zoneinfo does not know (as Outlook writes them) is read as local time; a floating time is local. Only the first value of a comma-separated list is read.
- RRULE is expanded with `dateutil.rrule.rrulestr(…).between(now - length, until, inc=True)`, and EXDATE moments are removed. A RECURRENCE-ID event in the same database row replaces the occurrence it changes; changed occurrences left over inside the window are added. SUMMARY unescapes `\,` only.
- `buddy_link` reads it at most once a minute, and only while it has fresh Odoo data to merge into. A read error logs `calendar: …` and keeps the last list.

### 3.7 Claude Code hooks and usage figures

The hooks in `~/.claude/settings.json` share one file, so the last session to write wins:

```text
Notification      permission_prompt|elicitation_dialog  mkdir -p ~/.cache/monitor-buddy && echo "needs ${CLAUDE_PROJECT_DIR##*/}" > ~/.cache/monitor-buddy/claude
PreToolUse        AskUserQuestion                       the same "needs" command
Stop              (all)                                 mkdir -p ~/.cache/monitor-buddy && echo "done ${CLAUDE_PROJECT_DIR##*/}" > ~/.cache/monitor-buddy/claude
UserPromptSubmit  (all)                                 rm -f ~/.cache/monitor-buddy/claude
PostToolUse       (all)                                 grep -qs needs ~/.cache/monitor-buddy/claude && rm -f ~/.cache/monitor-buddy/claude; true
```

`buddy_link` turns `needs` into `claude` 1 while the file is at most 1800 s old, and `done` into 2 while it is at most 90 s old.

On a computer set up with `./setup.sh` the same events call `tools/buddy_claude_hook.py needs|done|prompt|tool|end` instead of these shell lines; it writes the same two kinds of file (this one and the per-session ones below), needs no `jq`, never prints and always exits 0. The setup leaves hand-written hooks like the ones here alone (it looks for `monitor-buddy/claude` in the settings).

Each session also keeps its own file, `~/.cache/monitor-buddy/sessions/SESSION_ID`, holding `STATE FOLDER`. The id is the hook input's `session_id` with everything but letters, digits and `-` removed; without one the hooks write nothing. These hooks sit beside the ones above in the same events:

```text
UserPromptSubmit  (all)                                 working
Notification      permission_prompt|elicitation_dialog  needs
PreToolUse        AskUserQuestion                       needs
PostToolUse       (all)                                 working, only when the file says needs
Stop              (all)                                 done
SessionEnd        (all)                                 deletes the file
```

The write command, here for `working`:

```sh
id=$(jq -r ".session_id // empty" | tr -cd "A-Za-z0-9-"); [ -n "$id" ] && mkdir -p ~/.cache/monitor-buddy/sessions && echo "working ${CLAUDE_PROJECT_DIR##*/}" > ~/.cache/monitor-buddy/sessions/$id; true
```

`buddy_link` lists the files at most 3 hours old (`SESSION_MAX_AGE_S`), the last changed first, and sends up to 4 as STATE `sessions` (§6.3), each with the file's change time.

The usage figures are optional: `buddy_link` reads `~/.cache/claude-statusline.work` when a status-line script of your own writes it (format: `5H% 5H-RESET 7D% 7D-RESET EPOCH`; the resets are `HH:MM` and a weekday such as `Sat`, `-` when unknown).

### 3.8 Shell and Claude sessions

- `buddy-say` (a symlink to `tools/buddy_say.py`) resolves its imports through the script's real folder, `tools/`.
- `~/.config/fish/conf.d/buddy.fish` hooks `__buddy_done` to `fish_postexec`. When a command ran over 60 s, its first word is none of `vim nvim vi nano ssh claude man less more htop top btop watch tmux fish bash zsh python python3 ipython psql`, and `buddy-say` is on the PATH, it runs `buddy-say "DONE: WORD1 WORD2"` or `buddy-say "FAILED CODE: WORD1 WORD2"` in the background with its output discarded.
- A Claude Code session can be told in its instructions file to run `buddy-say` when a long job ends; the screen already shows waiting and finished sessions by itself.

### 3.9 Data flow and freshness

| Data | Source | Carried by |
|---|---|---|
| Screen lock | logind `LockedHint` (§3.4) | STATE `locked`, `in`, `out`, `yin`, `yout` |
| Microphone | PipeWire (§3.4) | STATE `mic` |
| Pull requests | `gh` GraphQL (§3.4) | STATE `prs`, `prs_failing`, `reviews`, `changes`, `prs_merged`, `prs_reviewed`, `pr_list` |
| Claude sessions | The hook files (§3.7) | STATE `claude`, `at`, `sessions` |
| Claude limits | The status-line cache while it is at most 5 h old (§3.7) | STATE `use`, `use_reset` |
| Odoo figures | The Chrome extension (§3.5) | The `O` packet |
| Google meetings | GNOME's calendar cache (§3.6) | Inside the `O` packet, without their links |
| Weather | Open-Meteo, fetched by the Buddy every 15 min | - |
| GitHub page | api.github.com and the public contributions page, without a token, fetched by the Buddy every 30 min | - |
| Stock and coin prices | Finnhub and Coinbase, fetched by the Buddy every 60 s while their page shows | - |

The Buddy forgets the laptop's STATE 180 s after the last one (`PC_STATE_MAX_AGE_MS`) and drops Odoo data 30 min after it arrived (`ODOO_STALE_MS`).

## 4. Build, flash and update

### 4.1 Toolchain and build

- `pio` is PlatformIO, installed as a uv tool (`~/.local/bin/pio`). PlatformIO's own environment, `~/.platformio/penv/`, holds `esptool` (v5.4.0) and `esp_coredump`.
- Environment `esp32-c6`: platform pioarduino 55.03.312-1 (Arduino core 3.3.12, ESP-IDF 5.5.5), pinned on purpose; board `esp32-c6-devkitc-1`; 8 MB flash in DIO mode; `partitions.csv`; LittleFS.
- Libraries: GFX Library for Arduino ^1.4.9, ArduinoJson ^7.2.0, AyresWiFiManager (IdefixRC fork, tag 2.5.0), U8g2 ^2.35.30, AnimatedGIF ^2.1.1.
- `-I config` lets `src/main.cpp` include `config/config.h`, or `config.h.example` when there is none.
- `FW_VERSION` is set only by upstream's release workflow, so a local build reports `dev`; the release image reports its version (1.0.0). What tells builds apart is the build id: the first 8 bytes of the ELF's SHA-256 (`app_elf_sha256`, at offset 0xB0 of `firmware.bin`).

```sh
pio run -e esp32-c6
```

The result is `.pio/build/esp32-c6/firmware.bin` (about 1.9 MB) and `firmware.elf`. A crash dump decodes only with the ELF of the build that crashed, so copy the ELF away before rebuilding while a crash is under investigation.

### 4.2 Flash layout (`partitions.csv`)

| Name | Type | Offset | Size | Holds |
|---|---|---|---|---|
| nvs | data/nvs | 0x9000 | 0x5000 | ESP-IDF's key-value store |
| otadata | data/ota | 0xE000 | 0x2000 | Which app slot boots, and whether it is on trial |
| app0 | app/ota_0 | 0x10000 | 0x300000 (3 MB) | Firmware slot 0 |
| spiffs | data/spiffs | 0x310000 | 0xE0000 (896 KB) | LittleFS (§8.3) |
| coredump | data/coredump | 0x3F0000 | 0x10000 (64 KB) | The last crash dump |
| app1 | app/ota_1 | 0x400000 | 0x300000 (3 MB) | Firmware slot 1 |

This is upstream's `huge_app` layout plus the second app slot. The bootloader sits at 0x0 and the partition table at 0x8000; the last megabyte is unused. A USB flash writes the bootloader, the partition table, `boot_app0` into otadata and the firmware into app0; a Wi-Fi update writes the slot that is not running. Neither touches LittleFS, so settings and Wi-Fi survive both.

### 4.3 Flash over USB

```sh
cd PROJECT_FOLDER
systemctl --user stop buddy-link
sg dialout -c 'pio run -e esp32-c6 -t upload --upload-port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_*'
systemctl --user start buddy-link
```

`sg dialout -c` is needed only while `id -nG` lacks `dialout`, that is, until the next login. A USB flash always wins: the new firmware boots from app0 with no trial (§4.5).

### 4.4 Update over Wi-Fi

```sh
pio run -e esp32-c6 && python3 tools/buddy_flash.py [FIRMWARE.bin]
```

It takes about 1.5 minutes, and `buddy-link` can keep running. The steps:
1. The tool pings the Buddy. When the running build id equals the image's, it stops with "The Buddy already runs this firmware."
2. It signs the image with `~/.config/monitor-buddy/update-key.pem` (ECDSA P-256 over SHA-256, deterministic per RFC 6979) and sends `B` with the size and the signature. The Buddy answers with the byte offset to go on from: 0 for a new update.
3. It sends the image in 1024-byte `D` pieces (§6.2), with 8 tries of 1 s per piece, and prints the progress about every 200 KB. The Buddy writes them into the slot that is not running and hashes them.
4. With all bytes in, the Buddy checks the signature against the `UPDATE_KEY` compiled into the firmware it runs. If it matches, the Buddy shows "UPDATED / restarting..." and restarts into the new slot after 300 ms. If not, it answers `refused` and drops the update, and the tool exits with "The Buddy refused the firmware: its signature does not match UPDATE_KEY, or the image is broken."
5. The tool then asks for health every 2 s for up to 90 s. "The Buddy runs the new firmware." means the build id matches. "The Buddy does not run the new firmware: it is back on the one before." means the Buddy restarted into the old build: a rollback (§4.5). When the Buddy did not restart, the last piece was lost and the tool sends `B` again. With no answer for 90 s it exits with "The Buddy does not answer since the last piece."

Resume: a firmware transfer that ends early is paused, unless writing the flash failed, and the Buddy keeps it for 10 minutes. The tool meanwhile keeps sending `B` and goes on from the offset the Buddy reports, until 10 minutes after it started; then it exits with "The Buddy stopped answering for 10 minutes. It keeps its firmware." Because the signature is deterministic, running the same command again within those 10 minutes also resumes where the Buddy stopped. A `B` with another size or signature starts over.

### 4.5 Rollback

The firmware declares `verifyRollbackLater()`, so a firmware that came over Wi-Fi boots on trial (`ESP_OTA_IMG_PENDING_VERIFY`; the log says `link: listening on UDP 4747, new firmware on trial`). The first link packet it accepts, of any kind, keeps it (`link: new firmware kept`); the next packet from `buddy-link` does that. A restart before then, for any reason (a crash at start, a power cut, an `@MB SET`), makes the bootloader go back to the previous firmware. This was tested with an `abort()` build, and the rollback is how the NTP start-up crash of §11.1 showed itself.

### 4.6 First install, keys and `./setup.sh`

No firmware image holds a key any more (unless `config.h` defines one, §8.1). A Buddy gets its keys over USB:

1. **Firmware.** A new board, upstream firmware or a restored flash dump first gets this firmware over USB: with the browser installer (`web/`, §4.7), with `esptool` and the release's factory image at offset 0, or with `pio run -t upload` (§4.3). Without keys the Buddy runs by itself; the link stays closed and every `B` is refused (`link: update refused: no update key`).
2. **Keys and helper.** `./setup.sh` (it runs `tools/buddy_setup.py`) on the Linux computer, with the Buddy on USB. In order, each step looking first at what is there:
   1. checks Python for `cryptography`, `serial` and `dateutil`, and stops with the `apt` line when one is missing;
   2. `buddy_keys.ensure()` makes the link key and the update key when they are missing (mode 600, folder 700) and never replaces one. A `LINK_KEY` found in `config/config.h` is taken over into the key file instead of making a new one;
   3. stops `buddy-link` if it runs (it holds the port), asks the Buddy for `INFO` and compares the key fingerprints (§6.6). Equal: nothing to do. No keys on the Buddy: `@MB KEYS`, sent once through `mb_serial.change()`, then `INFO` again to see the fingerprints. Other keys on the Buddy: it asks before replacing them, and with `--yes` or no terminal it leaves them. A firmware whose `INFO` has no `keys` is sent to the browser installer;
   4. writes `~/.config/systemd/user/buddy-link.service` (the `sg dialout` form only when the account is in `dialout` but this login is older than that), enables it and starts or restarts it. Without systemd user services it says how to start the helper by hand;
   5. links `~/.local/bin/buddy-say`;
   6. says which desktop features this computer can feed: `loginctl` (lock), `pw-dump` (microphone), GNOME with `gsettings` and `gdbus` (focus sync), a signed-in `gh` (pull requests), an Evolution calendar cache (meetings). A missing one is a "skipped" note, never an error;
   7. asks once each, and remembers in `setup.json`: Odoo (writes the native-host manifest of §3.5 for Chrome and Chromium; loading the extension stays a manual step it lists), Claude Code (merges the hooks of §3.7 into `~/.claude/settings.json`, keeping the file as it was in `settings.json.bak`; an unreadable file is left alone), fish (writes `~/.config/fish/conf.d/buddy.fish`, §3.8, unless one is there);
   8. waits up to 25 s for the address file, pings the Buddy over Wi-Fi, and prints "Works now" and "Left to do".
3. Flags: `--dry-run` and `--status` change nothing (they do not even open the port); `--ask-again` asks the optional questions again; `--yes` asks nothing; `--port PORT`; `--rotate-keys` (§8.6).

Resume: nothing is kept that the next run could not see for itself. Every step checks the real state (key files, the Buddy's fingerprints, the unit file, the symlink, the hook entries), so a run that stopped anywhere, or ran without the Buddy, is finished by running it again. `setup.json` only holds the answers and the times of the steps.

### 4.7 The browser installer and its site

`web/` is a static site: `index.html` loads `js/app.js`, which talks to the Buddy with Web Serial (`serial.js`, the `@MB` commands of §6.6) and installs firmware with the vendored `esptool-js` (`flasher.js`: the factory image at address 0; "Install" and "Fresh install" erase the flash first, "Update firmware" and "Install the firmware again" do not, so they keep settings, Wi-Fi and keys). `?fake=a` (also `b`, `c`, `d`, with the switches at the top of `fake-buddy.js`) runs every screen against a pretend Buddy.

This fork changed: the links (`strings.js`), the "Computer link" status on the home screen (from `INFO.keys`, §6.6), the closing summary "works now / left to do / optional extras" (`summaryItems()` in `form.js`, drawn by `summaryCard()` in `app.js`), and the new-board screen, which now leads with **Install**.

The site is `web/` plus a copy of each release's factory image and `firmware/releases.json` (version, size, SHA-256, settings version), built by `tools/pages_site.py OUT_DIR`: a browser cannot download release assets (no CORS headers), so Pages serves copies, and the page checks size and SHA-256 before it writes. The workflow `.github/workflows/pages.yml` runs it when a release is published, or by hand, and deploys to GitHub Pages (`https://abdaishere.github.io/monitor-buddy/`). For a look before a release exists: `python3 tools/pages_site.py /tmp/site --local v1.0.0=FACTORY.bin`, then `python3 -m http.server --directory /tmp/site` and open `http://localhost:8000/?fake=b`.

A release needs these files: `monitor-buddy-vX.Y.Z-esp32c6-factory.bin` (the installer and `esptool` at 0x0; `tools/release_image.py` checks it), `…-esp32c6-app.bin` (for `buddy_flash.py`), `…-sdcard.zip` and `SHA256SUMS.txt`. Build them with `PLATFORMIO_BUILD_FLAGS='-DFW_VERSION=\"X.Y.Z\"' pio run -e esp32-c6` in a checkout **without** `config/config.h`, so the image holds the example's defaults and no key.

### 4.8 The app (`tools/buddy_app.py`)

For people who do not use a terminal: `open-app.sh` (or the app-menu entry "Monitor Buddy") starts `buddy_app.py`, which serves `tools/app/index.html` on `127.0.0.1` at a port the system picks and opens it in the browser. Standard library only; one HTML file with its style and script inside.

- **Engine.** Nothing is done twice: `Recorder` is `buddy_setup.Setup` with its `step`, `ok`, `info`, `todo` and `would` kept as a list of events instead of printed. The page's tick boxes are written into `setup.json` as the answers before the run; the tick "pair it with this one instead" sets `repair_ok`, the only way the app replaces keys a Buddy holds. Rotation stays a terminal command. "Say" calls `buddy_say.say()`.
- **Make it yours.** `GET /api/settings` reads the Buddy's settings over USB (`Board.get_config()`), `POST /api/settings` sends the changed ones (`Board.set_config()`, one `@MB SET`). Every setting is there. The keys in `EDITABLE` pass both ways; `stock_key` and the Wi-Fi password are write-only (the page learns only that a key is set and the network's name); the link and update keys are not settings and cannot be sent. `GET /api/city?q=` asks Open-Meteo's search (the same one the installer uses) and adds the clock rule from `web/vendor/posix_tz_db/zones.json`. `buddy-link` is stopped while the port is used and started again. The Buddy's own checks are the validation; its errors are shown at their fields.
- **Endpoints.** `GET /` (the page), `/icon.svg` and the four face pictures; `GET /api/status` (a dry run: works, left, answers), `/api/progress` (the events so far, then works and left), `/api/ping`; `POST /api/setup`, `/api/say`, `/api/install` (`pkexec apt-get install -y` of the missing Python packages, when both exist).
- **Private.** Loopback only. Every `/api` request needs the header `X-Buddy-Token` with the random token of this run, which is in the address's fragment and nowhere in the page, and a `Host` of `127.0.0.1:PORT` or `localhost:PORT` (so another site, also by DNS rebinding, gets 403). The page sets a Content-Security-Policy without outside sources and writes everything with `textContent`. No answer ever holds a key (tested).
- **Life.** The page pings every 20 s; 180 s without a request and the program ends. A second start finds `app.json` and opens the running one. `--say` opens the second tab, `--no-open` only prints the address.
- **`--demo`** runs everything against a pretend Buddy in a temporary home folder (the key paths point there too): for a look and for the README's screenshots.
- **The package.** `python3 tools/make_deb.py VERSION [OUT_DIR]` builds `monitor-buddy_VERSION_all.deb` with `dpkg-deb`: the tools in `/usr/share/monitor-buddy` (with a `PACKAGED` marker and the zone table), the commands `monitor-buddy`, `buddy-say` and `monitor-buddy-setup`, a menu entry and an icon. It depends on `python3`, `python3-cryptography`, `python3-serial` and `python3-dateutil`, so nothing is missing afterwards. It has no install scripts and writes nothing into a home folder; with the marker, setup skips its own menu entry and `buddy-say` link. Tested in clean `ubuntu:24.04` and `debian:stable-slim` containers: install pulls the three libraries, the entry validates, the app serves and refuses requests without its token, setup makes the keys with mode 600, removal leaves no file. A .deb was chosen over Flatpak (its sandbox gets in the way of the serial port, the systemd user unit, and writing Chrome's and Claude's files) and over AppImage (no menu entry without extra tools).
- **Undo.** `monitor-buddy-setup --undo` (`./setup.sh --undo`) disables the service and removes what setup wrote: the unit, the menu entry and `buddy-say` link of a ZIP install, the native-host manifests, the fish hook when it is ours, and our Claude hooks (the settings file as it was goes to `.bak`). The keys stay unless `--forget-keys`.
- **Missing Python parts** (ZIP only): the status names the packages; "Install them" runs `pkexec apt-get install -y …`. In a container the named packages installed with exit 0 and the status then showed none missing; the password window itself needs a desktop and was not exercised.
- **First start from the ZIP.** A file manager will not run a script on a double click; in GNOME Files it is right-click, "Run as a Program". The .deb has no such step.

## 5. Every tool with its command

The Wi-Fi tools need `LINK_KEY` (§8.4), the address file and the Buddy on the laptop's network. When the Buddy does not answer they exit with "The Buddy does not answer at ADDRESS."

| Command | What it does |
|---|---|
| `pio run -e esp32-c6` | Build the firmware (§4.1) |
| `pio run -e esp32-c6 -t upload --upload-port PORT` | Flash over USB (§4.3) |
| `pio test -e native` | Unit tests on the PC (§13) |
| `./setup.sh [--dry-run] [--status] [--ask-again] [--yes] [--rotate-keys] [--port PORT]` | Set this computer up, or finish a setup (§4.6) |
| `./open-app.sh [--say] [--demo] [--no-open]` | The app (§4.8) |
| `python3 tools/buddy_keys.py` | Say which keys this computer holds, with their fingerprints (never the keys) |
| `python3 tools/buddy_flash.py [FIRMWARE.bin]` | Signed update over Wi-Fi (§4.4). FIRMWARE is a release's `-app.bin` or the local build; a factory image is refused |
| `python3 tools/buddy_flash.py --new-key` | Write a new signing key and print its `#define UPDATE_KEY "…"` line, for builds that compile the key in. It refuses when a key is there. `./setup.sh` uses the same function (`new_key()`) |
| `buddy-say TEXT…` or `python3 tools/buddy_say.py TEXT…` | Show a line on the Buddy (§7.8): `S` with the UTF-8 text cut at 110 bytes, 3 tries of 1 s; Wi-Fi only |
| `python3 tools/buddy_check.py [--card]` | Print the health answer (§6.5; 3 tries, 3 s timeout); `--card` also runs the card test (30 s timeout) |
| `python3 tools/buddy_shot.py OUT.png [PAGE]` | Screenshot over Wi-Fi: with PAGE it first sends `G` and waits 1.5 s, then fetches the screen as 86 row pairs and saves a 320×172 PNG |
| `python3 tools/buddy_put.py FILE.gif…` | Send animations: `F` with the size and the file name, then 1024-byte `D` pieces (§7.7 for where they land and the name rules) |
| `python3 tools/buddy_put.py --remove NAME.gif…` | Remove animations (`X`) |
| `python3 tools/buddy_gif.py IN.gif OUT.gif [RRGGBB]` | Offline: make a GIF fit the Buddy. It flattens transparency onto the colour (default black), centres the picture on the smallest canvas that fits (80×43, 160×86 or 320×172, which the Buddy draws 4×, 2× or 1×; larger pictures shrink with LANCZOS), uses one shared 64-colour palette without dithering, turns frame delays under 20 ms into 100 ms, loops forever, checks its own first frame and prints the file size |
| `python3 tools/buddy_art.py OUT_DIR [NYAN_SPRITE.gif]` | Offline: draw the Buddy's own animations `bounce.gif` (the Buddy's logo bouncing like an idle DVD logo), `starfield.gif`, `code-rain.gif`, `fire.gif`, `pong.gif`, `loading.gif` and the three reactions `full-day.gif`, `ci-green.gif` and `focus-done.gif`; with a sprite GIF of your own as the second argument also a `nyan-cat.gif` scene around it (the sprite is not part of this project) |
| `python3 tools/buddy_calendar.py [HOURS]` | Print the coming meetings (§3.6), one line each with day, start, end, name and link |
| `python3 tools/make_avatar.py PICTURE [--size 16..110]` | Write `src/gh_avatar.h`, an RGB565 circle (default 88 px); run from the repository root |
| `python3 tools/mb_serial.py PORT COMMAND` | Upstream's USB tool. COMMAND is `info`, `get [--show-key]` (the Finnhub key stays hidden without the flag), `set key=value…` or `set JSON` or `set @file.json`, `wifi` (§8.8), `reboot`, `defaults`, `monitor` (prints the log) or `selftest` (restarts the Buddy five times and leaves a `/config.json`). It opens the port with DTR and RTS low, sends a changing command once and checks it after reconnecting. Exit codes: 0 ok, 1 refused or no answer, 2 bad arguments. If it gets no reply, the script in §10.2 writes to the port directly |
| `python3 tools/buddy_link.py [PORT]` | The helper (§3.4); normally run only by the service |
| `systemctl --user stop`, `start`, `restart` or `status buddy-link` | Control the helper |
| `journalctl --user -u buddy-link -f` | Follow the helper's log |
| `python3 docs/guide/capture.py` | Over USB: screenshot every page and face (`@MB STATE`, `@MB SHOT`). Fixed to `/dev/ttyACM0`; calls `buddy_link.pull_requests()`. Stop `buddy-link` first |
| `python3 docs/guide/shots.py STEPS.json OUT.png [SHOT_DIR]` | Over USB and Wi-Fi: run the steps `{name, state or null, say, wait}` and lay the shots on one sheet. `state` is merged over a fake base STATE (3 pull requests, 2 reviews, 1 with changes, limits 62 % and 31 %); a failed shot comes out magenta. Step files: `steps_day.json`, `steps_game.json`. Stop `buddy-link` first |
| `python3 tools/pages_site.py OUT_DIR [--local TAG=FILE]`, `python3 tools/release_image.py …` | The GitHub Pages builder (§4.7) and upstream's factory-image checker |

## 6. Protocols

### 6.1 Wi-Fi link packets

- UDP to port 4747 at the address in `~/.cache/monitor-buddy/address`. There is no discovery.
- A request is `nonce (12 bytes) | AES-256-GCM ciphertext | tag (16 bytes)`, keyed with `LINK_KEY`, without additional data, 29 to 1400 bytes long. The nonce is the laptop's clock in milliseconds (8 bytes, big-endian) followed by 4 random bytes.
- The Buddy takes a packet only when the tag checks out, its millisecond stamp is greater than that of the last packet taken, and the stamp is within 60 s (`LINK_SKEW_S`) of the Buddy's clock. A stale packet logs `link: old packet dropped (is the clock right on both sides?)`; a packet with a bad tag is ignored without a word. The last stamp lives in RAM only, so it starts from 0 after every restart.
- The plaintext of an answer is `ok`, the build id (8 bytes), a status byte (bit 0: the timer runs; bit 1: the animations come from the card; bit 2: join the meeting on the face; bit 3: lock the laptop) and up to 1389 bytes of extra data. Bits 2 and 3 come only in the answer to a `P` or a STATE, each in one answer: a tap or a BOOT press sets them, the next such answer carries and clears them, and one still waiting after 10 s (`LINK_ASK_MS`) is dropped. It is sealed under the request's nonce with bit 7 of the first byte set, and sent as `ciphertext | tag` to the sender's address and port. `send_wifi()` in `buddy_link.py` returns what follows `ok`: bytes 0–7 are the build id, byte 8 the status, the rest the extra data.
- The Buddy listens only while Wi-Fi is connected, the Buddy holds a valid link key (§8.4) and the setup hotspot is closed.

### 6.2 Link messages

The first byte of the plaintext picks the message:

| First byte | Length | Meaning | Extra data in the answer |
|---|---|---|---|
| `{` | any | STATE (§6.3) | none |
| `P` | 1 | Ping | none |
| `S` | over 1 | Show the rest as a notice (§7.8) | none |
| `O` | over 1 | Odoo data (§6.4) | none |
| `H` | 1 | Health | JSON (§6.5) |
| `C` | 1 | Card test at 20 MHz | JSON (§6.5) |
| `G` | 2 | Show page N (0–15, §7.2) and hold the automatic flipping for 30 s | none |
| `R` | 2 | Screen rows N and N+1, N under 172 | the rows in RGB565, little-endian, 640 bytes per row |
| `B` | 6–85 | Begin or resume a firmware update: the size (4 bytes, big-endian) and a DER ECDSA signature of at most 80 bytes | the offset to go on from, as decimal text |
| `F` | over 5 | Begin an animation: the size (4 bytes, big-endian) and the file name | none |
| `D` | at least 5 | The next piece of an update or an animation: the offset (4 bytes, big-endian) and the data | none; `refused` for the last firmware piece when the signature check fails |
| `X` | 1 + name | Remove the animation of that name | none |

- A transfer takes one piece at a time. A `D` at the offset written so far is stored and answered; a repeat of the previous piece is answered again; anything else, other messages included, is ignored. The last piece is answered only after the signature check or, for an animation, the rename. 10 s without a piece ends the transfer.
- During a transfer the loop stops: the screen shows UPDATING or RECEIVING with the percentage in 5 % steps, and the face, the pages and the USB commands wait.
- An animation is written to `/gifs/NAME.part` and renamed over `/gifs/NAME` once complete; otherwise the part file is deleted. The Buddy rescans its animations after every `F` and `X`. An upload always starts at offset 0.
- No answer comes for an unknown message, invalid JSON, a bad name, a row out of range, or a refused `B` or `F`.
- Settings, Wi-Fi and restarts exist only over USB (§6.6).

### 6.3 STATE fields

`buddy_link` sends the keys down to `use_reset`; the others serve tests and screenshots.

| Key | Value |
|---|---|
| `locked` | The laptop's screen is locked; it also sets the privacy lock (§9.1) |
| `mic` | A program records from a microphone, seen on two polls in a row |
| `prs` | Your open pull requests |
| `prs_failing` | Those updated in the last 4 days whose last commit's checks are FAILURE or ERROR |
| `reviews` | Open pull requests that ask for your review |
| `changes` | Your pull requests with changes requested |
| `prs_merged` | Your pull requests merged on the working day before (§3.4) |
| `prs_reviewed` | Other people's pull requests that you reviewed and that were updated on that day |
| `pr_list` | Up to 4, updated last first. `t`: "#N title" without its leading `[…]` tags, at most 50 characters. `s`: checks 0 none, 1 passed, 2 failed or error, 3 running (PENDING or EXPECTED). `r`: 0 none, 1 changes requested, 2 approved |
| `claude` | 0 quiet, 1 a Claude Code session waits for you, 2 one has just finished |
| `at` | That session's project folder, at most 23 characters |
| `sessions` | Up to 4 open Claude Code sessions, the last changed first, as `[folder (23 characters), state, since]`: state 0 working, 1 waiting for you, 2 done; since in UTC seconds (§3.7) |
| `use` | `[5-hour %, 7-day %]` of Claude's limits |
| `use_reset` | When each resets, as the status line writes it: `["19:10", "Sat"]`, `""` when unknown |
| `in`, `out` | The first unlock and the last lock of today's work day, which runs from 05:00 to 05:00, as `HH:MM`, `""` before there is one. `buddy_link` keeps one row per work day in `days.csv` (§1, mode 600) and reads it when it starts; a lock seen after more than a minute without a look (the laptop slept) counts at the last look |
| `yin`, `yout` | The same of the row before today's in `days.csv`: the last day the laptop was used, so weekends drop out |
| `timer` | `true` or `false` starts or stops the focus timer |
| `mood` | 0–16: show that mood now and go to the first page; the next status change replaces it |
| `demo` | Force status N (§7.4); 16 is status 12 with the join caption; a number that is no status, such as 100, forces the plain face; 0 is off |
| `page` | Show that page (0–15) |
| `play` | Play that animation for 7 s |
| `tap` | Tap the current page |

### 6.4 Odoo packet

The Buddy reads the keys of §3.5 in `odooFromPc()`. It keeps at most 6 tasks, 3 task names of the day before and 3 meetings, cuts a trailing "(…)" from stage names ("Development (Team)" becomes "Development"), and takes `bw` as 1 (Monday) when it is missing.

### 6.5 Health and card-test answers

Health, the answer to `H`:

| Field | Meaning |
|---|---|
| `up_s` | Seconds since start |
| `reset` | Why it last started (`esp_reset_reason()`): 1 power-on, 3 software restart, 4 crash, 5–7 watchdog, 9 brown-out, 11 USB |
| `heap`, `heap_min` | Free heap now, and its lowest since start |
| `rssi` | Wi-Fi signal |
| `clock_set` | NTP has set the clock |
| `slot` | The running app slot, `app0` or `app1` |
| `fs_kb`, `fs_total_kb` | LittleFS used and total |
| `gifs`, `gifs_from` | The number of animations, and `internal` or `card` |
| `odoo_age_s` | Age of the Odoo data; -1 without |
| `weather`, `github` | That data is valid |
| `pc_locked` | The privacy lock is on |
| `crash_dump` | A valid crash dump is stored |
| `double_knocks` | Double knocks since start |
| `last_knocks_g` | Up to 8 knock strengths in g, newest first |

The card test (`C` over Wi-Fi; `@MB SD`, with an optional `hz`, over USB) answers `card`, `size_mb`, `sector0`, `signature`, `partition_type`, `partition_start`, `partition`, `used_mb`, `write_kb_s`, `read_kb_s`, `test` and `error`. It writes, reads back and deletes `/buddy-test.bin` (256 KB) on the card. The animations move to internal storage during the test and back to the card at the end; when no card answers or the card has no FAT file system, the test stops early and they stay internal.

### 6.6 USB `@MB` commands

- A request is one line: `@MB COMMAND`, optionally a space and a JSON object, then a newline. A reply is `@MB JSON`. Lines without the prefix are ignored, `\r` is ignored, and an `@MB` line over 3072 bytes (2048 before the page order joined the settings) gets the error "line too long".
- The JSON may carry an integer `id` from 0 to 2147483647, which the reply echoes. Every reply has `ok`, `cmd` (the command word, at most 16 characters), `id` when one was sent, and its own fields. The errors are "bad json", "bad id", "unknown command", and "restarting" for any command while a restart is pending.

| Command | What it does and returns |
|---|---|
| `INFO` | `fw`, `cfg_v` (2), `mac` (the station MAC), `wifi`: `saved`, `ssid`, `ssid2` when saved, `connected`, `rssi` when connected, and `keys`: `link` and `update`, each `false` or the key's fingerprint, 8 hex digits of SHA-256(`monitor-buddy link KEY` or `monitor-buddy update PEM`). `mb_serial.fingerprint()` makes the same sum |
| `GET` | `v`; `stored` (`/config.json` existed at boot or has been saved since); `config` with every setting (§8.2), the Finnhub key included; `added` (the keys the file lacked) |
| `SET` | Body `{"config": {some settings}, "wifi": {"ssid", "password"}, "wifi2": {…}}`. It checks everything first and on failure returns `errors` (path → message). Then it writes `/config.json`, `/wifi.json` and `/wifi2.json` atomically and restarts after 300 ms; a changed `brightness` also goes to `/display.json`. `ssid` is 1–32 characters; `password` is required: `""` for an open network, else 8–63 characters |
| `REBOOT` | Restarts after 300 ms |
| `DEFAULTS` | Deletes `/config.json`, keeps Wi-Fi, `/display.json` and the keys, restarts |
| `KEYS` | Body `{"link_key": 64 hex digits, "update_key": PEM public key}`, one or both; a key left out stays. Everything is checked first (`lib/keys`, and mbedTLS must be able to read the public key); on failure `errors`. Then it writes `/keys.dat` atomically and restarts. Write-only: no command returns a key, and the log never prints one. USB only: the Wi-Fi link has no message for it |
| `STATE` | The STATE of §6.3 |
| `ODOO` | The Odoo data of §6.4 |
| `SD` | The card test (§6.5) with an optional `{"hz": N}`; `ok` means it passed |
| `SHOT` | 172 lines `@SHOT Y BASE64`, each 640 bytes of RGB565 little-endian, then `ok` |

Before any restart the Buddy writes pending brightness and night-level saves.

### 6.7 Serial log

Every 1.2 s the Buddy prints a status line such as:

```text
app=0 mood=0 wifi=up ip=192.168.1.50 timer=0 fps=30 knock=0.04 up=5230s rst=3
```

`app` is the page (§7.2), `mood` the mood (§7.5), `knock` the largest jolt in g since the previous line, `up` the uptime and `rst` the reset reason (§6.5). Other lines start with their subsystem:
- `ESP32-C6 Monitor-Buddy dev starting` at boot.
- `mood: status N CAPTION` when the status changes; `knock: X.XX g` for each knock; `knock: two knocks` for a double knock; `tap: join the meeting` and `boot: lock the PC` for the requests of §6.1.
- `link: listening on UDP 4747` (with `, new firmware on trial` after an update), `link: new firmware kept`, `link: old packet dropped (…)`, `link: update of N bytes`, `link: update goes on at N of M`, `link: update paused at N of M`, `link: update dropped: …`, `link: update stored, restarting`, `link: update refused: …`, `link: paused update given up`, `link: animation refused (name or room)`.
- `settings: …` at boot and on SET or DEFAULTS; `gifs: N on the card` or `gifs: N in the internal file system`.

## 7. Pages and face statuses

### 7.1 The page round

`buildPageList()` makes the round at boot: first the switched-on pages in the order face, clock, date, weather, moon, stock, coin, GitHub; then always Odoo, Today, Claude, pull requests, animations, Bug Hop, AbdA. If every switchable page is off, the face comes back. A page with nothing to show is left out: coin without a usable coin slot (a switched-on Coinbase pair, or a Finnhub one with a key), GitHub with an empty `github_user`, stock without a key or a switched-on stock.

When moving on, `switchApp()` passes over the pull-request page while the laptop reports none (or sent no recent STATE), the Claude page without a recent STATE, the animations page while there are none, Odoo without valid data, and Today and Bug Hop unless a swipe made the move. AbdA is never passed over. With the stock, coin and moon pages switched off, for example, the automatic round is face, clock, date, weather, GitHub, Odoo, Claude, pull requests, animations, AbdA. The Yesterday page is in no round: only the briefing (§7.6), `G` and STATE `page` show it.

`updateAutoPage()` steps the round every 8 s (`PAGE_AUTO_INTERVAL_MS`) while automatic flipping is on; a double tap switches it, and a restart turns it back on. A pinned Today page (§7.6), at the briefing with the Yesterday page before it, stays until its time ends, then the round restarts at the first page. While a status holds the face (§7.4) or the privacy lock is on, the screen goes to the face once, when the hold begins and only if the face is the first page, and the flipping stops; swipes still change the page during a status hold. Any page change by swipe, tap, `G` or STATE `page` ends a pinned Today page.

### 7.2 Pages

| ID | Page | Shows | Tap |
|---|---|---|---|
| 0 | Face | The eyes in the current mood (§7.5) with the status caption (§7.4); while Wi-Fi is down the top line says WIFI OFFLINE or HOLD TO SET UP WIFI | Next of moods 0–8; during status 12 it asks to join the meeting (§7.4) |
| 1 | Clock | Seven-segment HH:MM with the colon lit on even seconds, small seconds, AM/PM in 12-hour mode | Next page |
| 2 | Date | Weekday, "MON DD", year | Next page |
| 3 | Weather | Icon, temperature and unit, condition, humidity "H n%", wind "W nKMH" or "W nMPH"; "UPDATING" until the first answer | Next page |
| 4 | Moon | Phase name, "AGE x.x DAYS", "LIGHT n%" and the disc, computed on the Buddy from the new moon of 2000-01-06 18:14 UTC and a 29.53058867-day month | Next page |
| 5 | Stock | Finnhub `/quote`: "$price" coloured against the previous close (or the open), the symbol, the day's %, OPEN/HIGH/LOW; dots on the right edge when 2 or more stocks are on | Next stock when 2 or more are on, else next page |
| 6 | GitHub | The avatar in a breathing ring with a comet, the streak in "DAY STREAK", then "n TODAY", "KEEP IT ALIVE" (a streak without today) or "0 TODAY", then stars, followers and repos. A second view is a 26-week heatmap | Switch to and from the heatmap once activity is known, else next page |
| 7 | Coin | "NOT FOUND" with the pair, or the price with $, £, € or the currency code. Coinbase: base code, %, high/low and a 24-hour hourly chart refreshed every 15 min. Finnhub: the stock layout | Next coin when 2 or more are on, else next page |
| 8 | AbdA | The fork author's signature: eight looping scenes of the word "AbdA", 5.2–7 s each; the terminal scene types `NAME@github:~$ whoami`, NAME being the `github_user` setting ("abda" when it is empty) | Next page |
| 9 | Odoo | `ODOO_TRIGRAM` and the time in the header; a ring of today's hours against `ODOO_DAY_HOURS`; OPEN TASKS and IN REVIEW counters; one task card with name, "#id n/N" and stage. "LOADING ODOO" without data | Next task when there are 2 or more, else next page |
| 10 | Pull requests | "PULL REQUESTS", "n open  m to review" and up to 4 pull requests, each with a check dot (grey none, teal passed, red failed, amber running) and a badge ("!" changes requested, a tick approved) | Next page |
| 11 | Animations | `/gifs/*.gif` in directory order, enlarged by the largest whole factor that fits, looped; a file that does not play is skipped | Next animation |
| 12 | Today | §7.3 | Next page |
| 13 | Bug Hop | §7.11 | Hop, or start a run, at touch-down |
| 14 | Claude | §7.3 | Next page |
| 15 | Yesterday | §7.3 | Next page |

The pages that need the internet (weather, stock, coin, GitHub) show a gate instead of their data: "SETUP MODE / JOIN WIFI / `AP_SSID`" while the hotspot is open, "WIFI SETUP / HOLD SCREEN 3s / TO CONFIGURE" with no saved network, and "CONNECTING / HOLD SCREEN 3s / FOR WIFI SETUP" otherwise.

The text pages (Today, Yesterday, Claude, pull requests, Odoo) share a slim 14 px grey header (`slimHeader()`), large helvB14 and helvB12 fonts (`drawParts()`, `wrapToWidth()`, `printCut()`), coloured stripes instead of row labels, and no page dots. A pull request takes two lines when there are 3 or fewer.

### 7.3 Today, Yesterday and Claude pages

Each row is a card with a stripe for its source (`pageRow()`): teal meetings, pink hours, grey GitHub, purple Odoo, orange Claude.

Today: the title (TODAY, GOOD MORNING, HELLO or DAY DONE) and the time, then four rows of 36 px:
1. Today's next meeting as "HH:MM name", passing over meetings that started more than 5 min ago and stopping at the first one on another day; "No more meetings today"; or "Waiting for Odoo" without Odoo data.
2. "in HH:MM" (`in`; DAY DONE adds "out HH:MM"), then "h:mm logged · h:mm focus · h:mm meet": the Odoo hours of today and the day totals of §7.9; the meet part shows only above zero. Parts that do not fit are left out from the end.
3. "n to review · n changes · n open" from the STATE, or "Laptop out of reach" without a recent one.
4. "n due · n tasks" (activities due, open tasks), or "Waiting for Odoo".

Yesterday: the title YESTERDAY and that day's name (`bw`), then rows of 28 px:
1. "HH:MM - HH:MM" from `yin` and `yout` ("in HH:MM" without `yout`), then "h:mm logged" from `hb`, or "Waiting for Odoo".
2. Up to 3 rows with the names of `y`, or "No task changed"; none without Odoo data.
3. "n merged · n reviewed" from `prs_merged` and `prs_reviewed`, or "Laptop out of reach".

Claude: the title CLAUDE and the time, then "Laptop out of reach" without a recent STATE, else:
1. Two rows "5h" and "7d", each with a bar (red from 80 %, amber from 50 %, teal below), the percent and "resets 19:10" or "resets Sat" from `use_reset`; one row "No figures yet" without `use`.
2. One row per session of `sessions`: the folder, and on the right the state and for how long ("waiting for you 3m", `formatSpan()`), teal working, amber waiting, grey done; "No open sessions" without one. With 4 sessions the rows are 23 px instead of 28.

### 7.4 Face statuses, in priority order

`updateBuddyMood()` runs every frame and shows the first status that applies. "Holds" means the status keeps the face on screen (§7.1). The status number is what STATE `demo` forces.

| Rank | Status | When | Mood | Caption (sub-caption) | Holds |
|---|---|---|---|---|---|
| 1 | 15 greeting | A double knock, for 2 s (`KNOCK_HELLO_MS`) | surprised | One of WHAT'S UP?, HELLO!, YES?, HEY THERE!, WHO'S THERE? | yes |
| 2 | 13 dizzy | A shake, until 1.3 s after the last jolt | dizzy | WHOAAA | yes |
| 3 | 14 unimpressed | A shake, from 1.3 s to 3.7 s after the last jolt | unimpressed | REALLY? | yes |
| 4 | 1 night | The night window (§7.10) | sleepy | - | no |
| 5 | 11 on air | The microphone is in use | on air | IN A MEETING m:ss | yes |
| 6 | 12 meeting soon | From 5 min before a meeting's start to 1 min after it (`MEETING_WARN_S`) | surprised | MEETING IN n MIN (rounded up), MEETING NOW from 60 s before the start (meeting name); OPENING THE LINK after a tap | yes |
| 7 | 7 focus | The timer runs | focus | FOCUS m:ss, h:mm:ss from an hour on | yes |
| 8 | 8 focused | For 15 s after the timer stops (`TIMER_DONE_SHOW_MS`) | excited | FOCUSED m:ss | yes |
| 9 | 2 lunch | The laptop is locked between 12:00 and 14:00 | lunch | LUNCH BREAK | yes |
| 10 | 3 away | The laptop is locked at another time | away | AWAY | yes |
| 11 | 9 Claude needs you | `claude` is 1 | surprised | CLAUDE NEEDS YOU (project) | yes |
| 12 | 4 log your hours | Monday to Friday, with valid Odoo data and the previous working day under half of `ODOO_DAY_HOURS` | nudge | LOG MONDAY'S HOURS, with that day's name | no |
| 13 | 5 CI red | `prs_failing` above 0 | CI | CI RED: n | no |
| 14 | 10 Claude done | `claude` is 2 | happy | CLAUDE DONE (project) | no |
| 15 | 6 locked in | Valid Odoo data with a task in a development stage | focus | LOCKED IN | no |
| - | 17–24 | The personal statuses of §7.12 sit at these ranks: welcome back (23) after rank 3; stretch (19) just before focus; lunch time (21), water (20) and the birthday or anniversary burst (17, 18) after Claude needs you; the idle ones (birthday, anniversary, message 24, weekend 22) only when nothing else applies | | | 19 and 23 hold |
| - | 0 | Nothing applies | normal | - | no |

- Before choosing, `updateBuddyMood()` rolls the day over (§7.9), drops a stale STATE and stale Odoo data (§3.9), and ends a finished shake or greeting with the Today page (§7.6).
- The caption and the sub-caption take turns every 2 s at the bottom of the face, the sub-caption in teal and the caption in grey; the sub-caption is folded to ASCII, at most 26 characters.
- The mood changes only when the status changes, which logs `mood: status N CAPTION`; in between, taps cycle moods 0–8. Statuses 13 and 15 also jump to the face when it is the first page. Other code sets the mood too: STATE `mood`, a spin (surprised), and the night's start and end (§7.10).
- A tap on the face during status 12 asks the laptop to open the meeting's link instead of changing the mood: it sets the join request of §6.1, logs `tap: join the meeting`, and the caption reads OPENING THE LINK for 4 s (`JOIN_SHOW_MS`). `buddy_link` picks the meeting and opens its link (§3.4). In demo mode and under the privacy lock the tap does not join.
- `demo` shows sample captions: 12:34 for statuses 7 and 11, 47:10 for status 8, "MEETING IN 4 MIN" with "Weekly sync" for status 12 (demo 16: OPENING THE LINK), and TUESDAY for status 4. Demo mode never holds the face.

### 7.5 Moods

| Mood | Eyes and motion | Extras |
|---|---|---|
| 0 normal | 90×90, plus the breath | - |
| 1 happy | 92×92, pupils 6 px up | Twin glints, pink cheeks |
| 2 surprised | 75×112, pupils jitter ±3 px | - |
| 3 sleepy | 95×75 | Heavy lids, a "zzz" icon |
| 4 angry | 85×80 | Red whites, frowning lids, an anger icon |
| 5 sad | 85×100 | Lids drooping at the outer corners |
| 6 excited | 90×106, plus the breath | Big pupils, gold glints, a gold sparkle |
| 7 love | 96×96 | Heart-shaped pupils, two hearts above |
| 8 suspicious | Left 90×50, right 90×105 | One squint, one wide |
| 9 focus | 92×58, pupils sweep from -14 to +14 px every 2.4 s | Frowning lids, a teal glint, a blinking cursor after the caption |
| 10 lunch | The bitmaps `LUNCH_FACE_A` and `LUNCH_FACE_B`, swapped every 260 ms | - |
| 11 away | An 8 px line rising and falling with the breath | Two z's drifting up on a 2.4 s cycle |
| 12 nudge | 85×100, pupils 8 px down, trembling ±2 px | Sad lids, a blue tear from the left eye every 1.6 s |
| 13 CI | 85×80, shaking ±3 px | Red whites, frowning lids, a red "!" flashing every 400 ms |
| 14 on air | No eyes | A red ON AIR sign alternating every 800 ms; the caption moves up to y 132 |
| 15 dizzy | 94×94, the head sways | Spiral pupils turning opposite ways, three gold stars circling |
| 16 unimpressed | 96×84, still, with a slow head shake | Flat half-shut lids with a grey edge, "..." growing every 500 ms |

The caption sits at y 148. Lunch and on air skip the spring physics and the Wi-Fi hint.

### 7.6 When the Today page comes up by itself

| Trigger | Title | Shown for |
|---|---|---|
| The day's first STATE with the laptop unlocked, from 05:00 (`briefed` in `/day.json`) | The Yesterday page (§7.3) for 15 s (`YESTERDAY_MS`), then GOOD MORNING before 12:00, else HELLO | 15 s, then 20 s |
| The laptop goes from unlocked to locked, from 17:00 | DAY DONE | 30 s |
| A shake answer ends | TODAY | 12 s |
| A knock greeting ends | TODAY | 12 s |

### 7.7 Reacting animations

| Trigger | Animation | Seconds |
|---|---|---|
| Odoo hours reach `ODOO_DAY_HOURS` (the data before was valid and below) | `GIF_FULL_DAY`, default `full-day.gif` | 9 |
| Failing pull requests drop to 0 while the laptop is unlocked | `GIF_CI_GREEN`, default `ci-green.gif` | 7 |
| A focus session of 25 min or more ends | `GIF_FOCUS_DONE`, default `focus-done.gif` | 7 |
| 30 min of status 3, AWAY (not in demo mode) | The bounce screensaver: not a file but `drawBounce()`, which draws the setting `name` (else BUDDY) beside two eyes in a box that changes colour at every wall. It needs no card | 60, then again |
| STATE `play` | the given file | 7 |

- The screensaver starts again each time it ends, and leaving status 3 stops it. A tap or a double knock stops it for one frame only.
- Animations play from `/gifs` on the card when the card mounted at boot, else from `/gifs` in LittleFS (about 800 KB of room). A name that is not there cancels the animation.
- A name is 5–40 characters of letters, digits, `.`, `-` and `_`, ending in `.gif`; an uploaded one may not start with a dot. An upload into LittleFS needs its size plus 16 KB free.

### 7.8 Overlays and banners

- A notice from `S` (`buddy-say`) shows above any page for 20 s (`NOTICE_MS`): at most 4 lines of 26 characters, folded to ASCII. While it shows, no animation plays.
- An animation from §7.7 shows above any page, below a notice.
- A tap or a double knock puts away the notice, or else the animation.
- A vertical swipe shows "BRIGHTNESS n%" or "NIGHT n%" for 1.2 s; a double tap shows "AUTO: ON" or "AUTO: OFF" for 1.4 s.

### 7.9 Focus timer and day totals

- The timer starts when the Buddy lies flat (§3.3), once per lying down, or on STATE `timer: true`. It counts up.
- It stops when the Buddy is stood up (only a timer the Buddy started itself), when the laptop locks (any timer), or on STATE `timer: false`.
- A session of 60 s or more adds to the day's focus time and session count. An on-air stretch of 60 s or more adds to the day's meeting time.
- `/day.json` keeps `date` (YYYYMMDD), `focus` and `meet` in seconds, `sessions`, `best` (the Bug Hop record) and `briefed`. A new local date resets everything except `best`.

### 7.10 Night mode and brightness

- The night window runs from `night.start` to `night.end` (local hours; it may cross midnight). In it the backlight drops to `night.brightness`, status 1 shows when nothing ranks higher, the mood turns sleepy at the start, and turns back to normal at the end if it is still sleepy.
- A press lights the screen at the day level for `NIGHT_WAKE_MS` (30 s when `config.h` sets none).
- The day level is 180 until `/display.json` is read, stays within 8–255, and is saved to `/display.json` 2 s after the last vertical swipe. The night level is saved to `/config.json` 2 s after the last swipe, unless a SET or DEFAULTS has replaced that file since boot.

### 7.11 Bug Hop

- During a run the automatic flipping and the motion-sensor reads pause.
- A press starts a run at 150 px/s from idle, or from the game-over screen once 700 ms have passed. During a run it hops: rise 500 px/s, gravity 1500 px/s². A press up to 130 ms before landing still hops.
- Speed is min(150 + 0.012 × distance, 340) px/s; the score is distance / 12.
- Small bugs come from the start, big ones above 40 points, pairs above 120, and wasps above 180. Wasps fly 44 px up; stay on the ground to pass under them.
- The hit box forgives 4 px at the sides and 3 px above and below.
- A hit shows SPLAT and updates the record (`best`, §7.9).

### 7.12 Personal touches (`lib/persona`)

All decisions and caption texts are in `lib/persona` (pure C++, 21 unit tests); `updateBuddyMood()` asks it and draws the answer with a face that already exists. The settings are the last eleven keys of §8.2, set in the installer's "You and me" box. Every default changes nothing, except that the lunch nudge and the weekend doze are on with their defaults (12:00, Saturday and Sunday).

| Status (`demo` number) | When | Face | Caption |
|---|---|---|---|
| 23 welcome back | The laptop unlocks after 10 min or more locked, for 3 s | surprised | OH! HI NAME |
| 19 stretch | The timer runs and `break_min` is above 0: the first minute after every `break_min` minutes; the timer runs on | happy | STRETCH BREAK? |
| 21 lunch time | A work day, inside the working hours, the first minute of the `work.lunch` hour | happy | LUNCH TIME? |
| 20 water | `water` on, a work day, the first 30 s of each working hour except the first and the lunch hour | happy | SIP OF WATER? |
| 17 birthday | The date is `birthday` (a 29 February birthday: the 28th in other years): 20 s every 10 minutes over the lower statuses, and all day when idle | excited | HAPPY BIRTHDAY!, taking turns with the name |
| 18 anniversary | The same on the day of `since`, from the first year on | love | n YEAR(S) TOGETHER |
| 24 message | Idle, `message` set: 12 s from minute 7 of every quarter hour | happy | the message |
| 22 weekend | Idle on a day off (`weekend`) | sleepy | WEEKEND |

- Nothing personal shows before the clock is set, in demo mode (except the status `demo` names), or over a higher-ranked status.
- **Greeting** (status 15): `personaGreeting()` makes the line from `greeting` (`mix`, `hello`, `yo`, `salam`, `custom`), `greeting_text` and `name`. The name is shown in capitals; the mix is the five old lines plus HEY NAME! when there is a name. The installer shows the same line (`greetingPreview()` in `web/js/form.js`; a test keeps the two in step).
- **Work hours**: `work.lunch` to two hours later is the lunch face's window (was 12:00–14:00), `work.end` is when DAY DONE starts (was 17:00), and the log-your-hours nudge follows the work days (was Monday to Friday).
- **Names**: `name` is the owner (greetings, birthday, the bounce screensaver). `buddy_name` is the Buddy: the start screen says "NAME / is waking up..." and, once everything is up, "NAME / is ready!" for 1.2 s.
- `since` is written by the installer the first time the personal settings are saved (`stampSince()`), from the computer's date.
- `docs/guide/persona_shots.py` builds `docs/guide/shots/persona_*.png`, `docs/images/personal.png`, `docs/images/faces.png` and the sheets in `docs/images/faces/` from the board screenshots in `docs/images/board/` (taken over USB: `@MB SET` for the face style and the names Sam and Pixel, `@MB STATE {"demo": N}`, then `@MB SHOT`). Only the bounce is drawn by the script.

### 7.13 Face styles, the party face and the wave

- **Styles** (setting `face`). The moods, sizes, springs and lids are the same in every style; a style changes how one eye is drawn (`drawEyeShape()`, `drawPupil()`, `drawGlint()`, `faceScanLines()`) and what is drawn around the eyes (`drawFaceExtras()`): **classic** as before; **cat** oval eyes with upright slit pupils (rounder when big), ears that sway with the gaze but do not blink, a nose and whiskers; **robot** square eyes with scan lines, a frame around each and a bolt at each side, square pupils and glints, no blush; **minimal** slim pills at 55 % width without pupils (hearts, spirals, the sparkle and the tear stay). The lunch face and the ON AIR sign are full-screen pictures and ignore the style.
- **Party** (mood 17, `MOOD_PARTY`): the happy eyes with confetti falling behind them (above the caption) and a hat on the right eye. The birthday status uses it; taps do not reach it.
- **Wave** (status 25): `@MB STATE {"wave": true}` (the installer sends it on its closing screen, to firmware that has the `face` setting) or `demo` 25. For 8 s the eyes move 24 px left, a hand waves at the right edge (`drawWave()`), and the caption is `I'M NAME!` taking turns with `HI OWNER!` (`personaMeet()`). It ranks above everything and holds the face.
- **Checked in the simulator** (`tools/sim`, the real firmware on a simulated board): `tests/test_faces.py` draws the 9 moods and 24 status faces in each style and compares them with `golden/face_STYLE_NAME.png` (132 pictures), plus the wave and a refused style. The installer's thumbnails (`web/img/face-*.png`, copied to `tools/app/img/`) are those frames. Also checked on a real panel: all 9 moods and 24 status faces in each of the four styles, screenshotted over USB into `docs/images/board/` (the sheets in `docs/images/faces/` are made from them).

## 8. Configuration and secrets

### 8.1 Build-time config and runtime settings

`config/config.h` (made from `config.h.example`) is compiled into the firmware. Its names fall in two groups:
- Defaults for the runtime settings: `MOUNTING_LOCATION`, `THEME`, `GITHUB_USER`, `TIMEZONE`, `TZ_RULE`, `TZ_OFFSET_HOURS` (used only when `TZ_RULE` is empty), `TICKER`, `STOCKKEY`, `LAT`, `LONG`, `TEMP`, `WIND`, `SHOW_FACE`, `SHOW_CLOCK`, `SHOW_DATE`, `SHOW_WEATHER`, `SHOW_MOON`, `SHOW_STOCK`, `SHOW_GITHUB`, `CLOCK_12H`, `NIGHT_MODE`, `NIGHT_START_HOUR`, `NIGHT_END_HOUR`, `NIGHT_BRIGHTNESS`. The coin page has no such name: its default is on, with BTC-USD from Coinbase.
- Build time only: `NIGHT_WAKE_MS`, `ODOO_TRIGRAM`, and the fallback keys `LINK_KEY` and `UPDATE_KEY` (used only by a board that was given no key over USB, §4.6; the release images define neither), plus three names this `config.h` leaves to the defaults in `src/main.cpp`: `ODOO_DAY_HOURS` (8), `AP_SSID` and `AP_PASS`. The build stops when `AP_PASS` is neither empty nor 8–26 characters, `AP_SSID` is not 1–26 characters, or the night hours are out of range or equal.

The runtime settings live in `/config.json` on the Buddy. At boot `loadSettings()` takes the `config.h` defaults and lays the file over them; stored values win, and the log names every key the file lacks. So once a Buddy has a `/config.json`, editing those `config.h` names and flashing changes nothing for the keys the file holds. Change a setting with `@MB SET` (§6.6), for example `python3 tools/mb_serial.py PORT set pages.moon=true`, or go back to the `config.h` values with `DEFAULTS`. `/display.json` holds the day brightness and wins over the `brightness` setting at boot.

### 8.2 Settings keys

In the order `/config.json` and `GET` list them:

| Key | Allowed values |
|---|---|
| `v` | The settings version, 2 |
| `mount` | `left` or `right` |
| `theme` | 0–4 |
| `brightness` | 8–255 |
| `github_user` | 0–39 letters, digits and `-`; empty hides the GitHub page |
| `tz_name` | 1–64 letters, digits and `/ _ + -` |
| `tz_rule` | 1–64 characters of a POSIX TZ rule |
| `ticker` | 1–10 of `A-Z 0-9 . -`; the same as the first stock |
| `stocks` | 1–5 entries `{symbol, on}`; the first must be on |
| `stock_key` | 0–64 letters, digits, `_` and `-`: the Finnhub key |
| `coins` | 1–5 entries `{pair, source, on}`: `pair` is 3–21 of `A-Z 0-9` with one inner `-`, `source` is `coinbase` or `finnhub`; the first must be on |
| `lat`, `lon` | Decimal text from -90 to 90 and from -180 to 180 |
| `clock_format` | `24h` or `12h` |
| `temp_unit` | `celsius` or `fahrenheit` |
| `wind_unit` | `kmh` or `mph` |
| `night` | `{enabled, start, end, brightness}`: hours 0–23 that differ, brightness 8–255 |
| `pages` | `{face, clock, date, weather, moon, stock, github, coin}`, each `true` or `false` |
| `name` | 0–12 letters, digits, spaces, `.` and `-`: the owner's name (greetings, birthday, the bounce screensaver of §7.7, which shows BUDDY without one). Default `YOUR_NAME` in `config.h` |
| `buddy_name` | The same rule: the Buddy's own name, shown when it starts |
| `greeting` | `mix`, `hello`, `yo`, `salam` or `custom` |
| `greeting_text` | 0–26 letters, digits, spaces and `. , ! ? ' -`: the line of `custom` |
| `work` | `{start, end, lunch}`: hours 0–23 (`end` 1–24), `start` before `end` |
| `weekend` | `sat-sun`, `fri-sat`, `fri` or `none` |
| `birthday` | Empty or `MM-DD` (digits and `-`; a text that is no date is ignored) |
| `since` | Empty or `YYYY-MM-DD`: the day the Buddy was set up |
| `break_min` | 0–120; 0 is off |
| `water` | `true` or `false` |
| `message` | Like `greeting_text` |
| `face` | `classic`, `cat`, `robot` or `minimal`: the face style (§7.13) |

The keys from `name` on were added without a new settings version (§7.12): a `/config.json` from before them loads unchanged, reports them in `added` and gives them their defaults; the installer offers them only to firmware whose `GET` lists them. With every text at its longest, the largest `GET` reply is 1,580 bytes and the largest `SET` line 1,603, of the 2,048-byte line.

### 8.3 Files on the Buddy

LittleFS lives in the `spiffs` partition (§4.2).

| Path | Content | Written by |
|---|---|---|
| `/config.json` | The settings of §8.2 | `@MB SET` (atomic: a `.tmp` file, then a rename) and night-level saves; deleted by `DEFAULTS` |
| `/display.json` | `{"brightness": n}`, the day level | Brightness swipes (a plain overwrite) and `SET` |
| `/wifi.json` | The first network, `{"ssid", "password"}` in plain text | The setup hotspot, `@MB SET` with `wifi` |
| `/wifi2.json` | The second network, in the same form | `@MB SET` with `wifi2` |
| `/wifi.swap` | A temporary file during a network swap | `chooseSavedWifi()` |
| `/keys.dat` | The link key and the update public key, as JSON. Its name does not end in `.json` on purpose: the hotspot's "erase" removes `*.json` | `@MB KEYS` (atomic); nothing reads it back out. `DEFAULTS` and `SET` never touch it; a USB install without erase keeps it, a "Fresh install" (erase) removes it |
| `/day.json` | The day totals (§7.9) | `daySave()`, atomic |
| `/gifs/*.gif`, `/gifs/*.gif.part` | Animations when no card is used; a `.part` file while one arrives | Link messages `F` and `X` |

The TF card needs a FAT file system.

### 8.4 Secrets

Names and places only; no value belongs in this file, in a chat, or in a command line that gets logged.

| Secret | Guards | Lives in | Read by |
|---|---|---|---|
| The link key, 64 hex digits (AES-256) | Every Wi-Fi link packet | `~/.config/monitor-buddy/link-key` (mode 600, folder 700) and `/keys.dat` in the Buddy's flash. In no firmware image, unless `config.h` defines `LINK_KEY` (`./setup.sh` takes such a key over into the key file) | The Wi-Fi tools, through `buddy_link.link_cipher()`; the firmware (`loadKeys()`) |
| The update signing key, an unencrypted ECDSA P-256 key in PKCS8 | Firmware updates over Wi-Fi | `~/.config/monitor-buddy/update-key.pem` (mode 600, folder 700); it never leaves the laptop | `buddy_flash.py` |
| The update key's public half, in PEM | - | `/keys.dat` on the Buddy (or `UPDATE_KEY` in `config.h`, compiled in) | The firmware |
| Wi-Fi passwords | Your networks | `/wifi.json` and `/wifi2.json` on the Buddy (§8.3); any full flash dump | The firmware |
| `AP_PASS` | The setup hotspot | Upstream's default in `src/main.cpp` | The firmware |
| The Finnhub key | Stock and coin prices | `STOCKKEY` in `config.h` and `stock_key` in `/config.json`, empty by default | The firmware |
| The www.odoo.com session | The Odoo figures | Chrome's cookies in the `Default` profile | The extension |
| The GitHub login | The pull-request figures | `gh`'s own store | `gh` |

Personal, though not secret: `LAT` and `LONG` in `config.h`, the content of `odoo.json`, and the journal's `sent` lines, which carry pull-request titles and project folder names.

### 8.5 Renew the Odoo login path

When the Odoo page leaves the round, or the Today page says "Waiting for Odoo":
1. `journalctl --user -u buddy-link | grep 'odoo:' | tail -1` gives the reason (§3.5).
2. For "signed out of www.odoo.com", open https://www.odoo.com in Chrome's `Default` profile and sign in. For "no news from Chrome for 30 minutes", start Chrome: the extension runs only while Chrome does. For "nothing from Chrome yet", set the extension up (below). Any other message is Odoo's own error text.
3. Reload the extension in `chrome://extensions` to read at once; otherwise the next alarm does it within a minute.
4. Check: `ls -l ~/.cache/monitor-buddy/odoo.json` shows the current minute, and the journal says `odoo: data from Chrome`.

To set it up again (a new Chrome profile or laptop):
1. In `chrome://extensions`, turn on Developer mode, choose "Load unpacked" and pick the project's `tools/chrome-odoo` folder. The manifest key keeps the ID the host manifest allows.
2. Write the host manifest of §3.5 to `~/.config/google-chrome/NativeMessagingHosts/com.monitor_buddy.odoo.json`; `tools/buddy_odoo_host.py` must stay executable.
3. Sign in to www.odoo.com in that profile.

After editing `worker.js` or `manifest.json`, reload the extension; Chrome runs the old code until then.

### 8.6 Rotate the keys

Rotate them when the Buddy, its flash or the key files may have been read by someone else. With the Buddy on USB:

```sh
./setup.sh --rotate-keys
```

It refuses, changing nothing, when the Buddy is not on USB, does not answer, or runs a firmware without `@MB KEYS`. Otherwise `buddy_keys.rotate()` moves both key files aside, makes new ones, and the Buddy gets them with `@MB KEYS`. When the Buddy shows the new fingerprints, the old files are overwritten and deleted and `buddy-link` restarts with the new key; when it does not, the old files are put back. Both keys always change together, and a plain `./setup.sh` never replaces a key.

### 8.7 Keys compiled into a build

A build of your own may still carry the keys (`LINK_KEY`, `UPDATE_KEY` in `config/config.h`); a key the Buddy got over USB wins over them. `python3 tools/buddy_flash.py --new-key` prints the `UPDATE_KEY` line for the key it makes and refuses when one exists. Such an image holds the link key, so it must not be shared; the release build is made without `config.h` for that reason (§4.7).

### 8.8 Change the Wi-Fi

- On the Buddy: hold the screen (§3.3), join the hotspot with the password the screen shows, open http://192.168.4.1 and pick the network. This writes `/wifi.json`.
- Over USB: `python3 tools/mb_serial.py PORT wifi` asks for the network name and the password (without echo) and sets the first network; the Buddy restarts. For the second network, put `{"wifi2": {"ssid": "…", "password": "…"}}` in a file only you can read, send it with `python3 tools/mb_serial.py PORT set @FILE`, and delete the file.
- A new network gives the Buddy a new address: re-learn it (§10.2).

## 9. Security model and residual risks

### 9.1 What protects what

- **The link.** AES-256-GCM with `LINK_KEY` keeps packets secret and unforgeable, and the stamp rule (§6.1) stops replays.
- **Firmware updates.** The Buddy installs only an image signed over its whole length with the key that stays on the laptop (§11.1). The rollback (§4.5) protects against a firmware that cannot reach the link, not against a hostile one.
- **Odoo** rests on Chrome's session (§3.5); it is the only Odoo credential, and it never leaves Chrome.
- **Calendar and GitHub** add no credentials of their own (§3.6, §3.9); pull requests use `gh`'s login.
- **The privacy lock.** `privacyLock` follows `locked` in every STATE and nothing else.
  - While it is on, these are blocked: page swipes and taps (the double tap and the join tap too), Bug Hop presses, the hold that opens the hotspot, the knock greeting with its Today page and the single-knock blink, and the Today page after a shake.
  - These still work: brightness swipes, putting away a notice or an animation by tap or double knock, the shake statuses, notices, `G` and STATE `page` (any page), STATE `mood`, the reacting animations, and the BOOT button's lock request.
- **The setup hotspot** is WPA2 (§3.2).

### 9.2 Residual risks

- **USB is the master key.** Anyone with a cable can run every `@MB` command (§6.6), `KEYS` included (so pair the Buddy with their own computer), read the whole flash with the link key and the Wi-Fi passwords in it (`@MB` never returns the keys, a flash dump does), and flash any firmware: there is no flash encryption and no secure boot. Keep the Buddy where strangers cannot plug in, and rotate the keys (§8.6) if it was out of sight.
- **Whoever has `LINK_KEY` on the same network** can fake STATE and Odoo data (and so lift the privacy lock), show notices, fill or empty the animation storage, run the card test, read health, and turn to any page with `G` and copy it with `R`, the locked laptop's Odoo, Today, Yesterday and Claude pages included. Answering the laptop's packets in the Buddy's place, they can make the laptop lock its screen or open the link of a meeting that starts within 10 minutes.
- **Replays and the clock.** Because the Buddy forgets the last stamp at a restart (§6.1), a packet recorded shortly before a restart can be played once more after it. After a cold power-on the link refuses everything until NTP has set the Buddy's clock.
- **The privacy lock** starts off after every Buddy restart, until the first STATE arrives; it stays on when the laptop goes silent while locked; and the DAY DONE page (§7.6) shows the day's meetings, hours and counts right after a lock.
- **Files at rest.** Anyone who can read your files can sign firmware with the update key; a full flash dump holds the Wi-Fi passwords and keys (§8.4); `~/.cache/monitor-buddy` and its `sessions` folder are mode 775, and the address, Claude and session files 664, so they are world-readable.
- **Meeting links come from invitations.** Whoever puts a meeting in your Odoo or Google calendar writes its link, and a tap on MEETING IN n MIN opens it in the browser, as a click in the invitation would. `buddy_link` opens only `https://` links.
- **The hotspot password** is upstream's default, published in section 4.9 of upstream's README.
- **The extension runs with your www.odoo.com session.** It only reads because its code only reads; review every change to `worker.js`.

## 10. Troubleshooting

### 10.1 Symptom, cause, fix

| Symptom | Likely cause | Fix |
|---|---|---|
| A Wi-Fi tool says "The Buddy does not answer at ADDRESS." | The Buddy is off Wi-Fi or on another network; its address changed; the hotspot is open; a transfer is running; the clocks differ by over a minute, or the Buddy has just been powered on and NTP has not answered yet (USB log: `link: old packet dropped`) | Look at the face for WIFI OFFLINE; re-learn the address (§10.2); wait a minute after a power-on; check the laptop's clock |
| A Wi-Fi tool says "This computer has no link key yet." | `~/.config/monitor-buddy/link-key` is missing or does not hold 64 hex digits (and `config.h` has no `LINK_KEY`) | Run `./setup.sh` with the Buddy on USB (§4.6) |
| The tools say "The Buddy does not answer" right after a setup | The Buddy holds other keys, or none | `./setup.sh --status`, then `./setup.sh` with the Buddy on USB |
| WIFI OFFLINE or HOLD TO SET UP WIFI on the face | The saved network fails (§3.2), or its password changed | Wait for the router; else change the Wi-Fi (§8.8) |
| The hotspot opened by itself | No network is saved any more | Set the Wi-Fi again (§8.8) |
| The Odoo page is missing, LOADING ODOO, or "Waiting for Odoo" on Today | No fresh Odoo data (§3.9) | §8.5 |
| "Laptop out of reach" on Today; lock faces and pull requests are missing | `buddy-link` stopped, or reaches the Buddy neither by Wi-Fi nor by USB | `systemctl --user status buddy-link`; its journal |
| `buddy-link` restarts every 10 s | `gh` is missing: `pull_requests()` does not catch that error | Install or repair `gh` |
| Odoo figures change only every 5 minutes | Chrome still runs an older copy of the extension | Reload it in `chrome://extensions` |
| A Google meeting is missing | GNOME has not synced it yet, or there is no fresh Odoo data to carry it (§3.5) | Open GNOME Calendar to sync; fix Odoo first |
| A meeting shows twice, or a declined single occurrence still shows | Limits of the calendar reader (§12.1) | - |
| `buddy_flash.py` waits 10 minutes and says "stopped answering", though `buddy_check.py` works | The running firmware refuses `B`, which gets no answer (§4.6, §6.2); a signature over 80 bytes or a slot that will not open does the same | Read the `link: update refused: …` line over USB; flash over USB (§4.3) |
| `buddy_flash.py`: "The Buddy refused the firmware …" | The image is not signed with the key the running firmware trusts, or it is broken | Check `update-key.pem` against `UPDATE_KEY`; §8.7 |
| `buddy_flash.py`: "… it is back on the one before." | The new firmware crashed at start or never reached the link | Read the crash dump (§10.2) |
| An update keeps breaking off | Weak Wi-Fi where the Buddy stands: one bad spot lost 20 % of packets, with Wi-Fi gone for about 20 s after each break | Run the command again (§4.4), move the Buddy, or flash over USB |
| UPDATING or RECEIVING stays on the screen | A transfer broke off (§6.2) | Wait 10 s |
| An upload: "The Buddy refused FILE: its name, or no room left." | The name breaks the rules, or there is no room (§7.7) | Rename it; remove animations; use the card |
| `gifs_from` says `internal` with a card fitted | The card did not mount at boot | Restart with the card in; `buddy_check.py --card` |
| A tap on MEETING IN n MIN opens nothing | The journal's `join: …` line says why: no meeting within 10 minutes in the laptop's list, or no `https://` link. Odoo links need extension 1.2 or later. With no `join:` line the request never reached the helper: `buddy-link` was down or the Buddy off Wi-Fi | `journalctl --user -u buddy-link \| grep join:`; `python3 tools/buddy_calendar.py` shows the Google links |
| BOOT does not lock the laptop | The request goes only over Wi-Fi and waits 10 s (§6.1); the journal shows `lock: …` or `could not do what the Buddy asked: …` | Check the link with `buddy_check.py`; press again |
| The Claude page lists a session that ended | Its SessionEnd hook did not run (a crash or a kill); the file stays until it is 3 hours old | Delete the file under `~/.cache/monitor-buddy/sessions/`, or wait |
| The USB flash cannot open the port | `buddy-link` holds it, the group is missing, or the `ttyACM` number changed | §4.3 as written: stop the service, `sg dialout`, the by-id path |
| No output on USB, a MAC of 00:00:00:00:00:00, esptool `Write timeout` | BOOT (GPIO9) is stuck down | §10.2 |
| The Buddy restarts when a serial tool opens the port (`rst=11`) | The tool asserts DTR and RTS | Use `mb_serial.py`, `buddy_link.py` or the script in §10.2 |
| No `buddy mood:` lines in the journal | They come only through the USB log | Plug the Buddy in |
| The timer starts by itself and notifications go quiet | The Buddy lies flat | Stand it up |
| Notifications stay off after the timer | The focus settings were not restored | `systemctl --user restart buddy-link` restores them from the focus file; else `gsettings set org.gnome.desktop.notifications show-banners true` |
| Knocks are missed, or typing counts as a knock | Detector limits | Retune from data (§3.3) |
| Text on other pages has the wrong font or size | A page left a u8g2 font set | The font rule in §3.2 |
| Bits of the hotspot screen stay after it closes | Nothing forces a full send then, so rows whose hash did not change keep old pixels | They clear at the next whole-frame send (§3.2) |
| A changed `config.h` setting has no effect after flashing | `/config.json` on the Buddy wins (§8.1) | `@MB SET`, or `DEFAULTS` |
| `crash_dump: true` in the health answer | A crash was stored | §10.2 |

### 10.2 Recipes

**Talk to the Buddy over USB.** Stop `buddy-link` first and start it again afterwards, and use the by-id path (§11.1). The first read brings a backlog of old log lines, and `stty -F` opens and closes the port and loses that burst. This script opens the port the way `buddy_link.py` does, drains the backlog, sends one line and prints the `@MB` replies; save it as `/tmp/mb.py`:

```python
import glob, os, sys, termios, time, tty
fd = os.open(glob.glob("/dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_*")[0], os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
tty.setraw(fd)
attrs = termios.tcgetattr(fd); attrs[2] &= ~termios.HUPCL; termios.tcsetattr(fd, termios.TCSANOW, attrs)
def pump(seconds):
    got, end = b"", time.monotonic() + seconds
    while time.monotonic() < end:
        try: got += os.read(fd, 65536)
        except BlockingIOError: time.sleep(0.01)
    return got
pump(1)
os.write(fd, sys.argv[1].encode() + b"\n")
print(*[line for line in pump(3).decode(errors="replace").splitlines() if line.startswith("@MB ")], sep="\n")
```

```sh
sg dialout -c "python3 /tmp/mb.py '@MB INFO'"
sg dialout -c "python3 /tmp/mb.py '@MB SET {\"config\":{\"pages\":{\"moon\":true}}}'"
```

`sg dialout -c` runs your login shell; with fish, which has no heredocs, put longer scripts in a file. Keep Wi-Fi passwords off the command line (§8.8).

**Stray serial readers.** Only one program can read the port. A forgotten background reader steals the `@MB` replies, and screenshots come out magenta: before USB work, check that nothing else has the port open.

**Re-learn the Buddy's address.** With `buddy-link` running, plug the Buddy into the laptop: within seconds the journal says `buddy address: …` and the file is updated. Most routers hand a known device the same address again. After writing the file by hand, restart `buddy-link`.

**Check which build runs.**

```sh
cd tools && python3 -c "import buddy_link as b; a = open(b.ADDRESS_FILE).read().strip(); r = b.send_wifi(b.link_cipher(), a, b'P'); print('running', r[:8].hex() if r else 'no answer'); print('built  ', open('../.pio/build/esp32-c6/firmware.bin', 'rb').read()[0xB0:0xB8].hex())"
```

**Read a crash dump.** esptool restarts the Buddy; mind the ELF (§4.1).

```sh
systemctl --user stop buddy-link
sg dialout -c '~/.platformio/penv/bin/esptool --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_* read-flash 0x3F0000 0x10000 core.bin'
~/.platformio/penv/bin/python -m esp_coredump --chip esp32c6 info_corefile --gdb ~/.platformio/packages/tool-riscv32-esp-elf-gdb/bin/riscv32-esp-elf-gdb --core core.bin --core-format raw .pio/build/esp32-c6/firmware.elf
systemctl --user start buddy-link
```

Afterwards, `esptool … erase-region 0x3F0000 0x10000` clears the stored dump, so that `crash_dump` reports only a new crash.

**BOOT stuck down.** GPIO9 is a strapping pin; held low at power-up, it keeps the chip from starting the firmware. Check with `~/.platformio/penv/bin/esptool --port PORT read-mem 0x6009103C`: bit 9 clear means the button is pressed. Only freeing the button helps. If the Wi-Fi was erased meanwhile, set it again over USB (§8.8).

**Test lock features.** Test them read-only, or lock with `loginctl lock-session`. Never run `loginctl unlock-session` in a test: it unlocks a laptop its owner has locked.

**Screenshots and what the panel shows.** `@MB SHOT`, `buddy_shot.py` and the guide scripts read the canvas, not the panel, so they cannot show what partial sends put on the glass; for that, look at the Buddy. With `buddy_shot.py`, a status that holds the face (Claude waiting, the laptop locked) can switch the page in the middle of a capture.

**A secret pasted into a Claude chat.** Take it out of the transcript with a script; never type it into a command. Blank it with an in-place overwrite of the same length (not `sed -i`: the live transcript is being appended to) in the `.jsonl` files under `~/.claude/projects/`, in `~/.claude/history.jsonl` and in `~/.claude/token-optimizer/checkpoints/`. New copies appear while that conversation stays open, so blank them again at the end of each turn. Then rotate the secret (§8.6, §8.7).

**Restore a full flash dump.** Writing a full dump made with `esptool read-flash 0x0 0x800000 FILE` overwrites the whole flash: firmware, settings, Wi-Fi, keys and animations. A dump of upstream firmware brings back upstream's layout without the second app slot; afterwards §4.6 applies.

```sh
systemctl --user stop buddy-link
sg dialout -c '~/.platformio/penv/bin/esptool --port /dev/serial/by-id/usb-Espressif_USB_JTAG_serial_debug_unit_* write-flash 0x0 FILE'
```

**Print the guide.** `python3 docs/guide/make_pdf.py` joins `guide.html` and `technical.html` and prints both PDFs. Each `section` starts a page; after a change, check with `pdftotext -f N -l N docs/Monitor-Buddy-Guide.pdf -` that no page holds a stray line.

**esptool from a Claude session.** Claude Code's auto-mode classifier may block an esptool run; run it yourself with `!` in that case.

**Upstream's warnings.** Holding BOOT while plugging the Buddy in starts the download mode. Upstream's own hosted web flasher installs upstream firmware and drops every feature of this fork; use this fork's installer (§4.7).

## 11. Design decisions

### 11.1 Decisions

| Decision | Why |
|---|---|
| Find the serial port by its by-id path | After a USB drop the board can come back under another number, such as `ttyACM1` |
| Detect the lock with logind's `LockedHint` | GNOME's `ScreenSaver.GetActive` stays false after Super+Escape and turns true only after `loginctl lock-session`, so tests that lock with `loginctl` pass while real locks fail |
| Draw the lunch face from a picture | Faces drawn with GFX shapes looked worse; a generated picture, cropped and warped by a script into the frames of `src/lunch_face.h`, works better |
| Take upstream's jobs from BOOT | BOOT can read low at a restart, and upstream's 5-second-hold rule then deletes `/wifi.json` and restarts; AyresWiFiManager's button moved to a free pin (§3.2). The timer starts by motion instead |
| A Wi-Fi link with USB as the fallback | The Buddy can sit on any charger. Settings stay on USB, which is also how a Buddy that lost its Wi-Fi is rescued |
| DHCP, no static address, no mDNS | A static address can clash on a shared network and breaks on a second network the Buddy knows; mDNS may be filtered on managed networks. Hence the address from the USB log (§3.4) |
| A second app slot with rollback (`partitions.csv`) | A bad image must not brick a Buddy away from the laptop |
| NTP gets addresses, not names | A start-up crash, lwIP's assert `udp_new_ip_type ... Required to lock TCPIP core functionality`, came when a fetch's `hostByName()` ran while SNTP had a DNS query open. Rollback and a core dump found it |
| The hours nudge looks at the previous working day | Today's hours are still being logged; yesterday's gap is the one worth a nudge |
| Odoo through Chrome, read only, no API key | Odoo API keys can be short-lived; every renewal meant a build and a flash, and the key sat on a board anyone with a cable can read. The browser session needs no key, and the Buddy never writes to Odoo |
| No deadlines on the Odoo page | A task's status is its pipeline stage; `date_deadline` is not kept up to date in every Odoo workflow |
| Signed updates | A leaked `LINK_KEY` (§9.2) must not be enough to install firmware. Tested: a wrong key is refused, the right one kept, and a cut transfer resumes where it stopped |
| A privacy lock that only an unlock clears | The pages show task names, meetings, pull-request titles and hours; nobody at the desk should browse them or open the hotspot while you are away |
| Fallback policy `NO_CREDENTIALS_ONLY` | A router that is slow after a power cut must not leave an open setup hotspot. A changed password is fixed with the hold |
| No secrets on the board image | Release images carry no key; keys arrive over USB (§4.6), and the Finnhub key is empty by default |
| 30 fps with partial sends | A page used to draw at 12 fps: a whole-frame send (§3.2), 10 ms of Wi-Fi upkeep, 3 ms of motion sensor, 2 ms of touch and a 24 ms delay. Bug Hop got `gameFlush()` first (29–40 fps); then every page got `flushChanged()`, and Wi-Fi upkeep became less frequent. The sensor reads and the eye springs kept their old step (§3.2), so the filters, the shake tuning and the feel of the face hold |
| The `millisSince()` rule | Stamps written as `millis() \| 1` sat 1 ms in the future and underflowed: the timer started at once instead of after 2 s flat, and the screensaver started at once |
| Knock detection | Desk knocks reach the board at only 0.08–0.25 g; knocks on its head at 0.1–2 g, after which it rocks on its stand, so an early version counted many double knocks for few. The jolt is the change of the whole acceleration vector, because the change of its length barely moves on a side knock (the same knocks read 0.35–0.6 g instead of 0.06–0.1 g). Typing must never count, which sets the threshold and the lead stillness (§3.3; a 400 ms lead never passed). Two fast knocks on one side twist the board, hence the gap range; slower pairs count too. Last test: 2 of 3 double knocks detected, a single knock ignored, no false detection |
| Google meetings from the Evolution Data Server cache | The GObject ECal bindings are often not installed and need root, so the tool parses the iCalendar text from the sqlite cache |
| Animations from the card, else LittleFS | A 16 GB SDHC card takes the animations over Wi-Fi in about 24 s and leaves the internal storage free |
| Focus sync restores on SIGTERM | Stopping `buddy-link` while the timer ran used to leave notifications off |
| Slim header and big text on Today, pull requests and Odoo | Readable from a normal sitting distance on a 1.47-inch panel |
| BOOT and meeting requests ride on the ping answers | A BOOT button down at start-up counts only after a release, because BOOT can read stuck down. The answers to the helper's pings come twice a second, so the Buddy still opens no connection, and meeting links stay on the laptop |
| Today leaves the automatic round | It still comes up by itself (§7.6) and by swipe, and the Claude page takes its place in the round |
| Bug Hop is reachable by swipe only | The automatic round must never land in a game |

## 12. Known limitations and open ideas

### 12.1 Limitations

- **Without `buddy-link`**, everything §3.9 lists as carried by STATE or the `O` packet goes missing after its time-out; the rest works on its own.
- **A new DHCP address** breaks the Wi-Fi tools until the Buddy is plugged in once (§3.4).
- **The second network is chosen at boot only**: moving between networks needs a restart (a `later:` comment in `src/main.cpp`).
- **GitHub:** stars count only the first 100 repos, and the heatmap uses fixed steps instead of quartiles of your year (both `later:` comments); GitHub allows 60 requests an hour per address without a token.
- **Calendar.** Google changes arrive only when GNOME syncs. The Evolution cache stores a changed occurrence in its own row, apart from its series, so the series still yields the original occurrence: a renamed or moved occurrence shows twice, and one declined or cancelled on its own still shows. RDATE is ignored. Duplicates between Odoo and Google go only on an exact name and start, and Odoo names are cut to 40 characters, so a longer name in both calendars shows twice.
- **Knock detection** misses some double knocks (last test in §11.1).
- **The Yesterday page** counts what Odoo and GitHub keep: a task changed on that day and again later has a later `write_date` and is missing, and a reviewed pull request counts on the day it was last updated, not the day of the review. At a first unlock right after a cold start it shows only what has arrived by then, so its Odoo rows may say "Waiting for Odoo".
- **Join and lock requests** are carried by one answer each; when that answer is lost on the Wi-Fi, the tap or the press does nothing and needs repeating.
- **`buddy-say`** can split a UTF-8 character at its byte cut (§5).

### 12.2 Where code and comments disagree

- The hold is 2500 ms in the code; the file header, `drawWifiGate()` and the hotspot screen say 3 s.
- The knock gap is 80–700 ms; its comment says 0.1 to 0.9 s.
- A shake needs five jolts within 800 ms; the comment says "four times within 0.8 s".
- The `setup()` comment says `run()` opens the hotspot "per the ON_FAIL policy"; the code sets `NO_CREDENTIALS_ONLY`.
- `platformio.ini` ("huge app, no OTA", first 4 MB only) and the `src/main.cpp` header (`huge_app.csv`) predate the second app slot.
- `lcdRegInit()`'s comment calls the panel AXS15231B.

### 12.3 Open items

- `docs/guide/capture.py` still opens `/dev/ttyACM0`; switch it to the by-id glob `shots.py` uses.
- Decide whether the DAY DONE recap (§7.6) should show while the laptop is locked.
- Retune the knocks (§3.3) if misses go on.
- A battery: the board has an ETA6098 charger but no cell fitted; its charge current is unknown, so use a protected cell and check the polarity first.
- Not built: Wi-Fi roaming while powered; following GitHub's Link header past 100 repos; a quartile heatmap.

## 13. Tests

```sh
pio test -e native
```

It builds `test/` with the host compiler against `lib/` (Unity, ArduinoJson 7) and passes all 153 cases:

| Suite | Cases | Covers |
|---|---|---|
| `test_persona` | 22 | The personal touches: work days per weekend choice, birthdays and anniversaries (leap days, things that are not dates), every greeting style and that each fits 26 characters, the stretch, lunch, water, message and weekend rules, nothing without a clock, and that the defaults stay quiet |
| `test_keys` | 4 | The link key is exactly 64 hex digits; the update key is a PEM public key of limited size with nothing but base64 and newlines inside |
| `test_protocol` | 20 | The `@MB` line reader (lines, `\r`, too-long lines with and without the prefix, a line of exactly the limit), command parsing, the 16-character command word, and argument parsing with `id` limits and bad JSON |
| `test_settings` | 32 | Settings load, merge and JSON round trip; brightness merge; time-zone rules from offsets; coordinate, time-zone and URL-character checks; Wi-Fi rules; the largest `GET` reply and `SET` line fit the line with a fifth to spare |
| `test_schema` | 27 | The table-driven schema engine on a demo struct: JSON order, group, list and record merges, load fall-backs, the reported paths, list checks, and the shared cases in `fixtures/settings-cases.json` |
| `test_compat` | 4 | Settings files across firmware 1.2.0 (a frozen copy of its settings code), 1.3 and a future version, both ways |
| `test_nightmode` | 5 | The night window, also across midnight; 12-hour hours; night-level steps; level percentages |
| `test_display` | 10 | Count formats (digits, k, M), the follower text size, the y of the entry dots, time spans |
| `test_odoo` | 9 | Hours as h:mm, stage names without "(…)", ASCII folding and cutting, Odoo's UTC times, word wrap |
| `test_coin` | 6 | Coinbase candles, price text, currency symbols, ISO UTC times |
| `test_github` | 6 | Stars summed across any chunk size, the contribution streak and today's count, days from a civil date |
| `test_button` | 4 | The BOOT button: one press per press, the debounce, a button down at the first read, the `millis()` wrap |

The automated tests stop at `lib/` and `tools/test_buddy_link.py` (the work-day bookkeeping and the STATE trim of `buddy_link.py`): the firmware in `src/main.cpp`, the rest of `buddy_link.py`, the other `buddy_*` tools, the calendar reader and the extension are checked by hand on the Buddy, with `buddy_check.py`, screenshots and the guide's step files.

`python3 -m unittest discover -s tools` runs 140 tests, all passing: `test_buddy_link.py`, upstream's tests of `mb_serial.py`, `pages_site.py` and `release_image.py`, and `test_buddy_setup.py` (34), which runs in a temporary home folder against a pretend Buddy:

- **Keys:** both are made with mode 600 in a folder of mode 700; a second run keeps them byte for byte; `new_key()` refuses to replace one; loose permissions are tightened; a broken key file is not replaced; a `config.h` key is taken over; the Buddy gets only the public half, in one `@MB` line; fingerprints do not contain the key; a rotation can be undone or made final; a signature made with the private key checks out against what the Buddy gets.
- **Setup and resume:** the first run pairs the Buddy; the second writes nothing, asks nothing and leaves the service running; a run without the Buddy is finished by the next one with the same keys; an old firmware is sent to the installer; a Buddy with other keys is left alone unless confirmed, and always with `--yes`; a dry run writes no file; rotation needs the Buddy, replaces the keys on both sides, and puts the old ones back when the Buddy refuses; missing desktop parts and a missing systemd are notes, not failures; the Claude hooks merge into existing settings once and leave an unreadable file alone; no run prints a key.
- **The Chrome host** (4): a popup's `say` is answered and not saved as Odoo data, an empty one refused, the Odoo summary still saved, and the extension keeps its id and permissions.
- **The app** (`test_buddy_app.py`, 31 with the Wi-Fi, city-search and Finnhub-key cases, over real HTTP on loopback): in "Make it yours" only the editable settings are shown and sent, the Buddy's refusals come back per field, and no Buddy or a silent port is explained; a status look changes nothing; a run pairs the Buddy and reports its 8 steps; the tick boxes are the answers and are remembered; a second run resumes with the same keys; a Buddy with other keys needs the tick; no answer holds a key; an unexpected error is shown; no token or another `Host` gets 403 and nothing happens; the page holds no token and no outside code; say trims, cuts and passes problems on.
- **The Claude hook** (`buddy_claude_hook.py`): a session from question to end, a session id that cannot leave its folder, and that it never fails or prints.

`node --test "web-tests/*.test.mjs"` passes 565 of 565 under Node 24 (`persona.test.mjs`: the greeting sample against the firmware's lines, the birthday fields, the set-up day, the rules of each personal setting, and that an older board is sent none of them) (the `bmp_github` check now skips the icon this fork's firmware no longer has; `summary.test.mjs` covers the closing summary).

Not covered by any automated test: the firmware side of `@MB KEYS` (`mbKeys()`, `loadKeys()` in `src/main.cpp`) beyond the shape checks in `lib/keys`. It is checked on a board: `./setup.sh`, then `python3 tools/buddy_check.py`.
