# Monitor Buddy: a desk companion that follows your workday

<p align="center"><img src="docs/media/demo.gif" alt="Monitor Buddy reacting on its keyboard dock (real video)" width="300"> <img src="docs/media/turntable.gif" alt="3D render: the dock turning" width="420"></p>

*A free, open-source hobby project: nothing is for sale. You'll need a Waveshare ESP32-C6-Touch-LCD-1.47 (touch version, ~US$15, e.g. from [Waveshare](https://www.waveshare.com/esp32-c6-touch-lcd-1.47.htm?sku=31203)); the two printed parts of the dock are optional.*

**Videos:** [launch video on YouTube](https://youtu.be/zsu_CKz4yd0) · [YouTube Short](https://youtube.com/shorts/FYTS_glE3H8) · [B-roll reel](https://youtu.be/cVD4qfim--U) · [both mounts reel](https://youtu.be/3k1B65LjsRc) · [real demo](docs/blog/images/demo.mp4) · B-roll (3D renders, 1080p) as release downloads: [turntable](https://github.com/Abdaishere/monitor-buddy/releases/download/v1.0.0/broll-turntable.mp4), [dolly-in](https://github.com/Abdaishere/monitor-buddy/releases/download/v1.0.0/broll-dolly-in.mp4), [rack focus](https://github.com/Abdaishere/monitor-buddy/releases/download/v1.0.0/broll-rack-focus.mp4), [exploded](https://github.com/Abdaishere/monitor-buddy/releases/download/v1.0.0/broll-exploded.mp4), [night](https://github.com/Abdaishere/monitor-buddy/releases/download/v1.0.0/broll-night.mp4).

**A 1.47-inch desk screen with a face that follows your working day**, on a printed keyboard dock. It chews during your lunch break, goes red when CI fails, says ON AIR while your microphone is in use, and looks up when Claude Code is waiting for you.

![Monitor Buddy on its keyboard dock, showing the date](docs/blog/images/cover-4x3.jpg)

This is a fork of [Monitor-Buddy](https://github.com/IdefixRC/Monitor-Buddy) by [IdefixRC](https://github.com/IdefixRC) (MIT, see [LICENSE](LICENSE) and [NOTICE](NOTICE)). The base firmware, the web flasher and the printed holder are their work; full [credits](#credits) are below. It adds firmware that follows GitHub, Google Calendar, Claude Code and your work hours, a laptop helper, AES-256-GCM sealed Wi-Fi, signed updates over the air, knock and shake gestures, and a two-part keyboard dock.

| | | |
|---|---|---|
| ![Docked](docs/blog/images/demo-1-on-stand.jpg) | ![Lifted off the dock](docs/blog/images/demo-2-lift-off.jpg) | ![3D render, three-quarter view](docs/blog/images/product-03-three-quarter.jpg) |
| ![LOCKED IN face](docs/blog/images/face-locked-in.png) | ![LUNCH BREAK face](docs/blog/images/face-lunch-break.png) | ![Today page](docs/blog/images/page-today.png) |
| ![3D render, on the keyboard](docs/blog/images/product-01-hero.jpg) | ![3D render, exploded](docs/blog/images/product-06-exploded.jpg) | ![3D render, on a desk at night](docs/blog/images/product-09-night.jpg) |

## What you need

- **The board:** Waveshare ESP32-C6-Touch-LCD-1.47, the **touch** version (8 MB flash, about US$15, e.g. from [Waveshare](https://www.waveshare.com/esp32-c6-touch-lcd-1.47.htm?sku=31203)). The version without touch is smaller and does not work.
- **A USB-C cable that carries data** (some charging cables don't).
- **A computer with Chrome or Edge**, once, for the install. Windows, macOS, Linux and ChromeOS all work.
- **The printed dock** (optional, two parts): print settings and assembly in [docs/blog/MAKERWORLD.md](docs/blog/MAKERWORLD.md). The model files are on MakerWorld only, as a remix of the original: [Monitor Buddy Keyboard Dock + Workday Firmware](https://makerworld.com/en/models/3664201-monitor-buddy-keyboard-dock-workday-firmware) (see [Licence](#licence)).
- Optional: a microSD card for the animations, and a Linux computer for the workday features.

## Set up

No programs to install and no code to build. Three steps; the first two take about five minutes.

### 1. Install from the browser

1. Plug the Buddy into your computer with the USB-C cable.
2. Open the installer in **Chrome or Edge**: **<https://abdaishere.github.io/monitor-buddy/>**
3. Click **Connect your Buddy** and pick it from the list (on Windows it has a COM number).
4. A new board is recognised as new: click **Install**.

| | |
|---|---|
| ![The installer's first screen: Connect your Buddy](docs/images/setup/01-start.png) | ![A new Buddy: choose Install](docs/images/setup/02-new-buddy.png) |

### 2. Tell it your Wi-Fi and your city

The page shows one form. Type your **Wi-Fi name and password**, search for **your city** (that sets the weather location and the time zone, daylight saving included), make it yours under **You and me** (all optional, see [below](#make-it-yours)), and pick the look you like. The preview on the left shows the result as you go. Then click **Install**.

One page in the round is the author's signature animation (AbdA). To leave it out, untick it in the laptop app's page list (step 3; the app's editor also runs on Windows).

| | |
|---|---|
| ![The city search fills in location and time zone](docs/images/setup/04-settings-city.png) | ![Progress while the firmware installs](docs/images/setup/05-installing.png) |

The page shows each stage (downloading, checking, writing, restarting). If something goes wrong it says what to do in plain words; an install that was cut off can simply be started again, and the BOOT and RESET buttons are never needed. At the end you get a summary of **what works now** and **what is left**:

![The closing summary: works now, optional extras](docs/images/setup/06-done.png)

That's it for a Buddy that lives on its own: the face, clock, date, weather, moon, GitHub page, the gestures (knock twice, shake, lay it flat for a focus timer) and the game all work. Unplug it and give it any USB power. To change something later, open the installer again and choose **Change settings**.

No Wi-Fi at hand? Skip it in the form: the Buddy opens a hotspot called **Monitor-Buddy-Setup**; join it with your phone (the password, `buddy1234`, is on the Buddy's screen) and pick your network there.

**Animations (optional):** unzip `monitor-buddy-v1.0.0-sdcard.zip` from the [latest release](https://github.com/Abdaishere/monitor-buddy/releases/latest) onto a FAT32 microSD card, unplug the Buddy, slide the card in and plug it back in (the card is read at start-up).

### Make it yours

Everything in the form's **You and me** box is optional, and skipping it leaves a perfectly good Buddy. You can change any of it later: open the installer again and choose **Change settings**.

![The "You and me" box of the installer](docs/images/setup/07-you-and-me.png)

| You tell it | What the Buddy does |
|---|---|
| **Your name** | Greets you with it, and after half an hour away your name bounces around the screen like the logo of an idle DVD player |
| **Its name** | Introduces itself when it starts: "Pixel is ready!", and waves hello at the end of setup |
| **Its face** | Classic eyes, a cat, a robot or two minimal pills. Every mood works in every style |
| **A greeting** | What it answers when you knock twice: a mix, HELLO, YO or SALAM with your name, or a line of your own. The form shows the exact words as you type |
| **Your working day and days off** | Asks "LUNCH TIME?" when lunch starts and dozes on your days off. With the computer link, the lunch face and the end-of-day page follow your hours too |
| **Your birthday** (no year) | A party hat, confetti and HAPPY BIRTHDAY on the day. It also remembers the day you set it up, and counts the years together |
| **Stretch reminder** | While the focus timer runs (lay the Buddy flat), asks for a stretch break every 25, 50 or 90 minutes |
| **Water reminder** | A short "SIP OF WATER?" on the hour during your working day. Off unless you switch it on |
| **A message for yourself** | Shows your line for a few seconds every quarter of an hour when nothing else is going on |

With the computer link it also says "OH! HI" when you come back after ten minutes or more away. Night mode (dimming and sleeping at night) and the five colour themes are in the same form, under **Clock** and **Look and pages**.

![What the personal touches look like on the screen](docs/images/personal.png)

*Screenshots read from a real Buddy over USB (320×172, the screen's own pixels), with the example names Sam and Pixel. Only the last one, the bouncing name, is drawn by a script: it appears after half an hour away.*

**Four faces.** One row per style: normal, happy, angry, sleepy, the birthday party, and the wave at the end of setup. These are screenshots read from a real Buddy over USB; the full sheets with every mood and status face are in [docs/images/faces](docs/images/faces).

![The four face styles: classic, cat, robot, minimal](docs/images/faces.png)

![Choosing the face in the installer](docs/images/setup/08-face-styles.png)

**Changing things later.** Everything here can be changed at any time, two ways: the installer page (**Change settings**), or the laptop app's **Make it yours** tab with the Buddy on its USB cable. Arabic is on the [roadmap](ROADMAP.md).

### 3. Connect your computer (optional, Linux)

This is what makes the face follow your day: away when the screen locks, ON AIR in a call, your pull requests, meetings, a quiet focus timer. It needs a Linux computer on the same Wi-Fi. (On Windows and macOS the Buddy works as in steps 1 and 2.)

1. Install the app: download **`monitor-buddy_1.0.0_all.deb`** from the [latest release](https://github.com/Abdaishere/monitor-buddy/releases/latest) and double-click it (Ubuntu and Debian; your software centre installs it together with the few system parts it needs; or in a terminal: `sudo apt install ./monitor-buddy_1.0.0_all.deb`). No .deb on your system? Choose **Code → Download ZIP** on the [project page](https://github.com/Abdaishere/monitor-buddy) and unpack it instead.
2. Leave the Buddy plugged into this computer by USB.
3. Open **Monitor Buddy** from your app menu (or, from the ZIP: right-click **`open-app.sh`** and choose **Run as a Program**). A page opens in your browser.
4. Tick the optional parts you want and click **Set up**.

| | |
|---|---|
| ![The app: optional parts and the Set up button](docs/images/setup/app-1-start.png) | ![The app after setup: every step, then what works now](docs/images/setup/app-3-done.png) |

The page shows each step as it happens and ends with the same kind of summary: what works, what is left. From then on **Monitor Buddy** is in your app menu. The page is served by a small program on your own computer and talks to nothing else; it closes by itself a few minutes after you close the tab.

**Make it yours.** The app's second tab is the Buddy's editor, for the first day and every day after. It reads the settings over the cable and shows the Buddy's screen next to them, live: change a face, a colour, a name, your city, a stock, and the screen on the page changes at once, before anything is on the Buddy. The picture is not a drawing of the firmware, it is the firmware: the same code the board runs, built for your browser, 320 by 172 pixels. Tap it, swipe it, knock on it.

![The editor: the Buddy's screen, live, next to its settings](docs/images/setup/app-5-yours.png)

- **Pages.** Drag a page to another place in the round (or use its arrows), untick one to leave it out, press **Show** to see it. All fifteen are there, the work pages too. (AbdA is the author's signature animation; untick it to leave it out.)
- **Everything else.** Names, face, greeting, working day, reminders, night mode, colours, brightness, which side of the monitor, your city (with a search), GitHub name, stocks, coins, Wi-Fi.
- **Apply to the Buddy** writes the changes over the USB cable; the Buddy restarts with them. Until then nothing on it changes, and **Discard changes** forgets them.
- **Undo the last Apply** puts back what the Buddy held before. Every Apply keeps a copy first (in `~/.config/monitor-buddy/backups`, never a key or a password); **Earlier settings** lists them.
- No Buddy plugged in? **Look around without a Buddy** opens the editor on a sample.

![The round of pages: drag to reorder, untick to hide](docs/images/setup/app-6-pages.png)

What the preview cannot know: prices, weather and the work pages (work hours, pull requests, Claude) show sample numbers, and the Animations page is empty (the GIFs are on the Buddy's card). A Buddy with firmware from before the page order keeps the usual order until it is updated: the editor says so, and the preview still shows the order you chose.

**On Windows.** The editor, the preview and Apply work there too: install [Python 3](https://www.python.org/downloads/), run `python -m pip install pyserial`, choose **Code → Download ZIP** on the [project page](https://github.com/Abdaishere/monitor-buddy), unpack it and double-click **`open-app.cmd`**. The Buddy is found on its COM port by itself (`open-app.cmd --board COM5` names it). The background helper of the first tab, and with it "Say something", is for Linux.

**Say something.** The app's third tab shows a line on the Buddy: type it, or click one of the quick buttons. (Right-click the app's icon for a "Say something" shortcut.)

![The app's Say something tab](docs/images/setup/app-4-say.png)

**Pictures.** The fourth tab puts your own picture on the GitHub page instead of your GitHub one (and back), and adds or removes the animations of the GIF page. Any GIF will do: it is made to fit the screen first. Both go over Wi-Fi, so the cable can stay out.

**From Chrome.** With the "Chrome extension" box ticked, setup tells you how to add the extension; its toolbar button opens a small box that shows a line on the Buddy, with the same quick buttons.

**Removing it.** Uninstall "monitor-buddy" in your software centre. To also take the background helper and the hooks out of your account, run `monitor-buddy-setup --undo` first (from the ZIP: `./setup.sh --undo`). Your keys stay unless you add `--forget-keys`.

Prefer a terminal? `./setup.sh` does the same setup, and `buddy-say tea is ready` the same as the tab. Either way, the setup:

- makes two secret keys **on your computer** and gives them to the Buddy over the USB cable. They are never shown, never sent over Wi-Fi and never leave your machine;
- starts a small helper in the background that tells the Buddy what your computer is doing;
- adds the app to your app menu, and the `buddy-say` command;
- sets up the optional parts you chose: work hours and tasks (Chrome), Claude Code sessions, the fish shell hook, and a firewall opening (UDP 4747, from the Buddy's network only) so that when the router gives the Buddy a new address, it tells your computer by itself instead of needing the USB cable.

Things worth knowing:

- **Run it again whenever you like.** It skips what is done and never replaces your keys. Stopped halfway, or the Buddy was not plugged in? Run it again and it carries on.
- **Missing something?** If your desktop is not GNOME or has no PipeWire, the features that need them (the quiet focus timer and Google Calendar meetings need GNOME, ON AIR needs PipeWire) are skipped with a note; everything else still works.
- From the .deb the command is `monitor-buddy-setup`; from the ZIP it is `./setup.sh`. `./setup.sh --dry-run` only shows what it would do. `./setup.sh --status` shows what works. `./setup.sh --ask-again` asks the optional questions again.
- If a small part of Python is missing, the app offers an **Install them** button (your computer asks for your password), and `./setup.sh` prints the one line to install it.
- `open-app.sh --demo` opens the app with a pretend Buddy and changes nothing: a safe way to look around.
- The preview is built from the firmware by `tools/sim/build_wasm.sh` (see [tools/sim](tools/sim/README.md)); the two files it makes are in the repository, so the app needs no compiler.

### Updates

- **Easiest:** plug the Buddy in, open the [installer](https://abdaishere.github.io/monitor-buddy/) and choose **Update firmware**. Settings, Wi-Fi and keys are kept.
- **Over Wi-Fi, no cable** (after step 3): download the file ending in `-app.bin` from the [latest release](https://github.com/Abdaishere/monitor-buddy/releases/latest) and run `python3 tools/buddy_flash.py monitor-buddy-vX.Y.Z-esp32c6-app.bin` (from the .deb: `python3 /usr/share/monitor-buddy/tools/buddy_flash.py` with the same file). The update is signed with your own key; the Buddy refuses anything else, and goes back to the old version by itself if the new one does not start.

### If something does not work

| What you see | What to do |
|---|---|
| The Buddy is not in the browser's list | Try another USB cable (it must carry data) and another USB port. Only the touch version of the board works. |
| The page says it needs Chrome or Edge | Firefox, Safari and phones cannot talk to USB devices. Use Chrome or Edge on a computer. |
| Wi-Fi did not connect | Network names are case-sensitive. Choose **Change settings** and type it again; the Buddy needs a 2.4 GHz network. |
| "Run as a Program" is not in the right-click menu (ZIP) | Open the file's Properties and switch on "Executable as Program", or use the .deb, which needs no such step. |
| Setup says "No Buddy found on USB" | Plug it into this computer and run it again. If it still fails, the message tells you whether your account needs USB access (one command, then log out and in). |
| Setup says the firmware cannot take keys | The Buddy runs an older firmware. Do step 1 again with **Update firmware**, then run the setup again. |
| The face stopped following the computer | Open **Monitor Buddy** from the app menu: it shows what works and what is left. |

To swap the keys for new ones (for example after lending the Buddy to someone): plug it in and run `monitor-buddy-setup --rotate-keys` (from the ZIP: `./setup.sh --rotate-keys`).

## More

- **The story**, with diagrams and measurements: [docs/blog/POST.md](docs/blog/POST.md)
- **Everything it can show**, page by page: the [PDF guide](docs/Monitor-Buddy-Guide.pdf)
- **How everything works** (for maintainers): [docs/HANDOVER.md](docs/HANDOVER.md)
- **Credits and licences**: [Credits](#credits) below, [docs/blog/CREDITS.md](docs/blog/CREDITS.md) and [NOTICE](NOTICE)
- **What may come next**: [ROADMAP.md](ROADMAP.md)

### Build it yourself (developers)

You only need this to change the code. With [PlatformIO](https://platformio.org/install/cli):

```
pio run -e esp32-c6 -t upload                  # build and flash over USB
./setup.sh                                     # keys and helper, as above
pio run -e esp32-c6 && python3 tools/buddy_flash.py   # later: signed update over Wi-Fi
pio test -e native && python3 -m unittest discover -s tools && node --test "web-tests/*.test.mjs"
```

The tests need Python 3 with `cryptography`, `pyserial`, `python-dateutil` and `Pillow`, and Node 24.

Settings and keys live on the board, not in the build: `config/config.h` (copy it from `config.h.example`) only changes the defaults of a board that has no saved settings.

## Licence

- **Code** (firmware, tools, web flasher): MIT, see [LICENSE](LICENSE). Copyright IdefixRC for the original, Abdallah El Belkasy for this fork's changes.
- **3D models**: not in this repository. The dock's head is built on IdefixRC's holder, which is under the MakerWorld Exclusive License, so the dock is published [on MakerWorld only](https://makerworld.com/en/models/3664201-monitor-buddy-keyboard-dock-workday-firmware), as a remix of the [original model](https://makerworld.com/en/models/3241960-monitor-buddy).
- **Animations** in `sdcard/gifs`: drawn by `tools/buddy_art.py`, MIT like the code.
- **Photos, renders and diagrams** in `docs/blog`: by Abdallah El Belkasy.

## Credits

- **The original**: [Monitor-Buddy](https://github.com/IdefixRC/Monitor-Buddy) by [IdefixRC](https://github.com/IdefixRC) and its contributors [AmyJoT](https://github.com/AmyJoT) and [Killo1988](https://github.com/Killo1988), MIT licence. The base firmware, the settings engine, the web flasher and the [printed holder](https://makerworld.com/en/models/3241960-monitor-buddy).
- **Where the original came from**: the Tiny ESP DeskBuddy idea from [schematik.io](https://schematik.io/) and additional face designs from [DESKBUDDY-1.0](https://github.com/EDISON-SCIENCE-CORNER/DESKBUDDY-1.0) by Edison Science Corner.
- **Firmware libraries**: [Arduino_GFX](https://github.com/moononournation/Arduino_GFX), [ArduinoJson](https://arduinojson.org/), [U8g2](https://github.com/olikraus/u8g2) and its fonts, [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF), [JPEGDEC](https://github.com/bitbank2/JPEGDEC), [AyresWiFiManager](https://github.com/ayresnet/AyresWiFiManager) in the [IdefixRC fork](https://github.com/IdefixRC/AyresWiFiManager), [Unity](https://github.com/ThrowTheSwitch/Unity), and the Arduino core for ESP32 through [pioarduino](https://github.com/pioarduino).
- **Browser installer**: [esptool-js](https://github.com/espressif/esptool-js) by Espressif (Apache-2.0) and [posix_tz_db](https://github.com/nayarsystems/posix_tz_db) by Nayar Systems (MIT).
- **Laptop tools**: [cryptography](https://cryptography.io/), [python-dateutil](https://github.com/dateutil/dateutil), [Pillow](https://python-pillow.org/), [pyserial](https://github.com/pyserial/pyserial) and the [GitHub CLI](https://cli.github.com/).
- **Data**: weather and the city search by [Open-Meteo](https://open-meteo.com/) (CC BY 4.0), stock quotes by [Finnhub](https://finnhub.io/), coin prices by [Coinbase](https://www.coinbase.com/).
- **Renders**: lighting maps and the wood texture from [Poly Haven](https://polyhaven.com) (CC0).

What each part is used for is in [docs/blog/CREDITS.md](docs/blog/CREDITS.md); the licence texts are in [THIRD-PARTY-LICENSES.md](THIRD-PARTY-LICENSES.md).

## The original project

This fork keeps the original's spirit and a lot of its code. The original README (the monitor clip, building with VS Code, the original controls) is at [IdefixRC/Monitor-Buddy](https://github.com/IdefixRC/Monitor-Buddy#readme). Its web flasher and releases install the original firmware, not this fork's.
