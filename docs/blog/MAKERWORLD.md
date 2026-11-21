# Monitor Buddy Keyboard Dock + Workday Firmware

**It reacts to you.** Knock twice on its head and it says HELLO! Lift it off the dock and lay it flat, and it starts a focus timer. Click it back in, and it goes back to watching your day.

**A free, open-source hobby project.** Nothing here is for sale: the model, the firmware and the laptop tools are all free.

**You'll need:** the two printed parts (about 1 h 24 min, 23 g), a Waveshare ESP32-C6-Touch-LCD-1.47 (touch version, ~US$15, e.g. from [Waveshare](https://www.waveshare.com/esp32-c6-touch-lcd-1.47.htm?sku=31203)) and a USB-C data cable.

**What works where:**

| | Windows / Mac / Linux | Linux (parts need GNOME) |
|---|---|---|
| Face, 9 moods, clock, date, weather, GitHub page, moon | yes | yes |
| Knock, shake, lay flat for the focus timer | yes | yes |
| Animations from the microSD card, Bug Hop game | yes | yes |
| Installing the firmware and changing settings, from the browser (Chrome or Edge) | yes | yes |
| The laptop app's editor: a live preview of the screen, page order, every setting | Windows and Linux | yes |
| Workday features: meeting warning, ON AIR, screen lock, CI, pull requests, Claude Code, Odoo hours | no | yes |

The workday features come from a small helper on a Linux laptop. Screen lock, pull requests, Claude Code and Odoo work on any desktop; ON AIR needs PipeWire, and the quiet focus timer and Google Calendar need GNOME. Windows and macOS ports of the helper are welcome on GitHub.

**A remix of [Monitor-Buddy by IdefixRC](https://makerworld.com/en/models/3241960-monitor-buddy).** Same board, the same snug screen holder, now on a click-in dock that sits on your keyboard, with new firmware that follows your working day.

A 1.47-inch touchscreen with a face that reacts to what you are doing. It chews during your lunch break, goes red when CI fails, says ON AIR while your microphone is in use, nags you to log yesterday's hours, and looks up when Claude Code is waiting for you. Knock twice on its head and it says hello. Lift it off the dock and lay it flat, and it starts a focus timer.

![Monitor Buddy on its dock in front of a keyboard, showing THURSDAY NOV 19 2026](images/cover-4x3.jpg)

- **The full story**, with diagrams and measurements: https://abdaishere.com/prints/monitor-buddy/
- **Firmware and laptop tools** (MIT): https://github.com/Abdaishere/monitor-buddy
- **The original model and firmware**: [MakerWorld](https://makerworld.com/en/models/3241960-monitor-buddy), [GitHub](https://github.com/IdefixRC/Monitor-Buddy)

## What is different from the original

- **A two-part dock instead of a monitor clip.** A slim 60 × 31 × 7 mm base and a head that holds the screen leaning back toward you. The head drops into the base, slides 8 mm and clicks. Slide it back and it lifts off, cable and all.
- **The buttons stay free.** The BOOT and RST buttons sit behind small pin slots, so seating the board no longer presses them.
- **A slightly wider pocket** (26.1 × 45.3 mm), so the board goes in without force.
- **New firmware** (optional: the dock also works with the original firmware).

![The Buddy on its dock](images/demo-1-on-stand.jpg) ![The head slid out of the base](images/demo-2-lift-off.jpg)

![Lifted off, still running](images/demo-3-in-hand.jpg) ![Back on the dock](images/demo-4-back-on-stand.jpg)

## What the firmware does

- **More than 30 faces**: 9 moods you tap through and over 20 status faces it picks by itself. The work ones, highest priority first: knock greeting, shake, night, on air, meeting soon, focus timer, timer done, laptop locked (LUNCH BREAK or AWAY), Claude needs you, log your hours, CI red, Claude done, locked in. The personal ones: welcome back, stretch, lunch time, water, birthday, anniversary, weekend, your own message.
- **Work pages**: Odoo hours and tasks, a Today page (next meeting, hours, reviews, Claude Code usage), pull requests with check and review marks, and a YESTERDAY card at the first unlock of the day.
- **Everyday pages**: clock, date, weather, GitHub streak and heatmap, a signature page, and the Bug Hop game.
- **Gestures**: knock twice for a greeting, shake for a dizzy face, lay it flat to start a focus timer that silences notifications, stand it up to stop it.
- **Animations** from the microSD card, nine drawn for this screen and included. Three play by themselves: when your hours pass eight, when CI turns green, and after a long focus session. A bouncing-logo screensaver starts after 30 minutes away.
- **Make it yours**, all optional: your name and the Buddy's, four face styles (classic, cat, robot, minimal), the greeting it answers a knock with, your working day and days off, a birthday party face, stretch and water reminders, a message of your own.
- **An editor on your laptop**: the app shows the Buddy's screen live next to its settings (the same firmware, running in the page), lets you drag the pages into your own order, and writes the changes over USB, with undo.
- **30 frames a second**: only changed screen rows are sent to the display.
- **Secure by design**: an AES-256-GCM sealed Wi-Fi link, firmware updates signed with a key that never leaves your laptop, automatic rollback of a firmware that fails to start, and a privacy lock while your laptop is locked.

![Locked in](images/face-locked-in.png) ![Lunch break](images/face-lunch-break.png) ![The Today page](images/page-today.png)

![The four face styles, screenshots from a real board: classic, cat, robot, minimal](../images/faces.png)

## The model from every side

These are 3D renders, not photos. The dock is drawn from the model files you download; the board, the USB-C cable, the keyboard, the monitor and the desk are modelled. The screens are real screenshots of the firmware.

![3D render: Monitor Buddy on the back edge of a keyboard](images/product-01-hero.jpg)

![3D render: three-quarter view](images/product-03-three-quarter.jpg) ![3D render: front](images/product-02-front.jpg)

![3D render: side](images/product-04-side.jpg) ![3D render: back](images/product-05-back.jpg)

![3D render: exploded view of base, head, board and USB-C cable](images/product-06-exploded.jpg) ![3D render: the empty dock with the board beside it](images/product-07-out-of-dock.jpg)

![3D render: the two printed parts, the board and the cable laid out](images/product-08-flat-lay.jpg) ![3D render: on a desk at night](images/product-09-night.jpg)

![3D render: close-up of the screen](images/product-10-screen-closeup.jpg)

## Parts (BOM)

| Part | Qty | Notes |
|---|---|---|
| Waveshare ESP32-C6-Touch-LCD-1.47, 8 MB flash | 1 | The **touch** version (~US$15, e.g. from [Waveshare](https://www.waveshare.com/esp32-c6-touch-lcd-1.47.htm?sku=31203)): the non-touch board is smaller and does not fit |
| microSD card | 1 | FAT32, any small card; it holds the animations. Optional: without it the Buddy simply has no animation page |
| USB-C cable and a USB port or charger | 1 | The cable must carry data for the first flash; after that any charger, monitor or keyboard USB port powers it |
| Felt or removable foam tape, about 1.5 mm thick | 2 strips of 5 × 24 mm | For the two recesses under the base, so it grips and does not mark the keyboard |
| Printed head | 1 | `dock_head_v8.stl` |
| Printed base | 1 | `dock_base_v8.stl` |

No screws, no glue, no magnets.

## Print settings

Printed on a Creality K1 Max in black silk PLA; plain PLA or PETG work too. The included 3MF plate has both parts in the right pose.

| Setting | Value |
|---|---|
| Layer height | 0.16 mm, first layer 0.24 mm |
| Nozzle | 0.4 mm |
| Head | Standing on its end as in the plate, 6 walls, 25% infill |
| Base | Flat on its underside, 100% infill |
| Supports | Normal (auto), build plate only, 30° threshold |
| Brim / raft | None |
| Temperatures (silk PLA) | Nozzle 225 °C, bed 60 °C |
| Time and filament | About 1 h 24 min and 23 g for both parts |

![3D render: the two parts as printed, base flat and head standing on its end](images/render-7-print-pose.jpg)

Good to know:

- Keep the head in the plate's pose. The neck then prints as one solid column and the two sliding lugs as vertical walls, which is what makes them strong.
- The supports touch one end face of the head's foot, so that face shows support marks. It faces sideways when docked.
- The fit of the click is tuned for 0.25 mm clearance per side. If your printer runs tight, scale nothing: sand the two lugs lightly instead.

## Assembly

1. **Clean up.** Remove the supports under the head's foot and from the relief under the base. Check that the two lugs under the head are clean and that the springy tongue in the base's groove moves freely.
2. **Pad the base.** Stick the two felt or foam strips into the recesses under the base.
3. **Card first.** If you want the animations, unzip the release's `monitor-buddy-v1.0.0-sdcard.zip` onto a FAT32 microSD card and put it into the board now: its slot is on the back and is covered once the board is in the head.
4. **Fit the board.** Slide the board into the head, screen forward, with the USB-C port at the open end, and press it down until it sits flat. Nothing should press on the two side buttons; you can reach them through the pin slots with a paperclip.
5. **Plug in** the USB-C cable through the opening in the head.
6. **Dock it.** Put the base on your keyboard's top strip or on the desk. Hold the head 8 mm to one side of centre, drop its two lugs into the two openings of the groove, and slide it toward the stop until it clicks. To take it off, slide it back and lift.

The base was shaped for the top strip of a Redragon K550 (it has a shallow relief underneath for the keyboard's badge). It sits just as well on any flat surface 60 × 31 mm or larger.

## Installing the firmware

No programs to install and nothing to build: it is done from the browser.

1. **Install.** Plug the board into a computer with a USB-C cable that carries data. Open the installer in Chrome or Edge, **<https://abdaishere.github.io/monitor-buddy/>**, click **Connect your Buddy**, pick it from the list and click **Install**.
2. **Wi-Fi and city.** Fill in the one form: your Wi-Fi name and password, your city (the search sets the weather location and the time zone), the look you like. Click **Install** again and wait for the summary of what works.
3. **Animations (optional).** Already on the card from assembly step 3. Adding the card later? Unplug the Buddy first: the card is read at start-up.

That is a complete Buddy: face, clock, weather, GitHub page, gestures, animations and the game. It works on Windows, macOS and Linux.

**The workday features** (screen lock, ON AIR, pull requests, meetings, Claude Code, Odoo) need a Linux computer on the same Wi-Fi:

4. **The laptop app.** Keep the Buddy on USB. On Ubuntu or Debian, download `monitor-buddy_1.0.0_all.deb` from the [latest release](https://github.com/Abdaishere/monitor-buddy/releases/latest) and double-click it; on other systems download the project from [GitHub](https://github.com/Abdaishere/monitor-buddy) (Code → Download ZIP), unpack it and run `open-app.sh` (or `./setup.sh` in a terminal). Open **Monitor Buddy** and click **Set up**. It makes the two secret keys on your computer, gives them to the Buddy over the cable, starts the helper, and asks once about the optional parts (Odoo, Claude Code, fish shell). Parts your desktop cannot feed (no GNOME, no PipeWire) are skipped with a note. Run it again at any time: it only does what is left.
5. **Updates.** Open the installer and choose **Update firmware**, or, without a cable, `python3 tools/buddy_flash.py` with the release's `-app.bin`: a signed update over Wi-Fi in about 90 seconds, which goes on where it stopped if the Wi-Fi breaks.

The full guide with screenshots is in the [README](https://github.com/Abdaishere/monitor-buddy#set-up). IdefixRC's own [web flasher](https://idefixrc.github.io/Monitor-Buddy/) installs the original Monitor-Buddy firmware, not this one.

## Good to know

- **USB is the master key.** The board has no flash encryption or secure boot, so anyone with a cable can read or reflash it. Keep it where strangers can't plug in.
- **Screens in the pictures show demo data**, not real work data.
- **Questions and bugs:** open an issue at https://github.com/Abdaishere/monitor-buddy/issues.
- **Bring your own GIFs.** `tools/buddy_gif.py` fits any GIF to the 320 × 172 screen. The nine included animations were drawn by a script for this project.

## Credits

Based on [Monitor-Buddy by IdefixRC](https://makerworld.com/en/models/3241960-monitor-buddy): the holder this dock's head is built on, and the [firmware](https://github.com/IdefixRC/Monitor-Buddy) (MIT) this one grew from. Additional face designs of the original came from [DESKBUDDY-1.0 by Edison Science Corner](https://github.com/EDISON-SCIENCE-CORNER/DESKBUDDY-1.0). Thanks to IdefixRC and the original's contributors, AmyJoT and Killo1988. Firmware libraries: Arduino_GFX, ArduinoJson, U8g2, AnimatedGIF and AyresWiFiManager. The browser installer uses esptool-js by Espressif and posix_tz_db by Nayar Systems. Weather data by [Open-Meteo](https://open-meteo.com/) (CC BY 4.0). The full list with licences: [CREDITS](https://github.com/Abdaishere/monitor-buddy/blob/main/docs/blog/CREDITS.md).
