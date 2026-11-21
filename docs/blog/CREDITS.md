# Credits

## The project it started from

- **[Monitor-Buddy](https://github.com/IdefixRC/Monitor-Buddy)** by IdefixRC and its contributors, MIT licence (Copyright (c) 2026 IdefixRC). The base firmware, the settings engine, the web flasher and the printed holder ([MakerWorld model](https://makerworld.com/en/models/3241960-monitor-buddy)). The MIT licence text must travel with any copy of the code.
- **[Tiny ESP DeskBuddy](https://schematik.io/)** from schematik.io, the idea upstream started from.
- **[DESKBUDDY-1.0](https://github.com/EDISON-SCIENCE-CORNER/DESKBUDDY-1.0)** by Edison Science Corner, the face designs upstream builds on.

## Firmware libraries

| Library | By | Licence |
|---|---|---|
| [GFX Library for Arduino](https://github.com/moononournation/Arduino_GFX) (Arduino_GFX) | moononournation | see its repository |
| [ArduinoJson](https://arduinojson.org/) | Benoît Blanchon | MIT |
| [U8g2](https://github.com/olikraus/u8g2) and its fonts | olikraus | BSD-2-Clause for the code; the fonts carry their own licences, listed by U8g2 |
| [AnimatedGIF](https://github.com/bitbank2/AnimatedGIF) | Larry Bank, BitBank Software | Apache-2.0 |
| [AyresWiFiManager](https://github.com/ayresnet/AyresWiFiManager), [IdefixRC fork](https://github.com/IdefixRC/AyresWiFiManager) | Daniel Salgado (AyresNet); fork by IdefixRC | MIT |
| [Unity](https://github.com/ThrowTheSwitch/Unity) (unit tests) | Mike Karlesky, Mark VanderVoord, Greg Williams | MIT |
| Arduino core for ESP32 and ESP-IDF, through the [pioarduino](https://github.com/pioarduino) platform | Espressif and the pioarduino community | see their repositories |

## Laptop tools

| Package | Used for | Licence |
|---|---|---|
| [cryptography](https://cryptography.io/) | AES-256-GCM link, ECDSA update signatures | Apache-2.0 or BSD-3-Clause |
| [python-dateutil](https://github.com/dateutil/dateutil) | Repeating calendar events | Apache-2.0 or BSD-3-Clause |
| [Pillow](https://python-pillow.org/) | GIF fitting, screenshots | MIT-CMU |
| [pyserial](https://github.com/pyserial/pyserial) | USB link of the app, the setup and `mb_serial.py` | BSD-3-Clause |
| [GitHub CLI](https://cli.github.com/) (`gh`) | Pull requests, reviews and checks | MIT |

## Browser installer and live preview

| Part | By | Licence | Used for |
|---|---|---|---|
| [esptool-js](https://github.com/espressif/esptool-js) | Espressif | Apache-2.0 | Writes the firmware from the browser |
| [posix_tz_db](https://github.com/nayarsystems/posix_tz_db) | Nayar Systems | MIT | Time zone rules for the city search |

The live preview in the app (`tools/app/sim`) is the firmware and the libraries above, compiled with [Emscripten](https://emscripten.org/) (MIT).

## Services

- **[Open-Meteo](https://open-meteo.com/)**: weather. Its API data is licensed CC BY 4.0, so a published build credits Open-Meteo.
- **GitHub**: the public profile and contributions page the Buddy reads, and the API behind `gh`.
- **NTP**: pool.ntp.org and time.cloudflare.com.

## Animations

Everything in `sdcard/gifs` is mine, drawn by `tools/buddy_art.py`, and free to share with the project: `bounce.gif` (the bouncing Buddy logo), `starfield.gif`, `code-rain.gif`, `fire.gif`, `pong.gif`, `loading.gif` (the "DOWNLOADING COFFEE..." install bar) and the three reactions `full-day.gif`, `ci-green.gif` and `focus-done.gif`.

No third-party animation is part of this project. The classic internet GIFs I play on my own card (Nyan Cat by Chris Torres, the Party Parrot, the "Peanut Butter Jelly Time" banana, a Hampster Dance hamster, a dancing skeleton, an "under construction" sign) belong to their creators. They were removed from the repository, with their screenshots, before it was published; add your own with `tools/buddy_gif.py`.

## The printed dock

The dock's head is built on the holder of [Monitor-Buddy by IdefixRC](https://makerworld.com/en/models/3241960-monitor-buddy), which is under the MakerWorld Exclusive License: derivative works may be published on MakerWorld only, with attribution, and not used commercially. The dock's model files are therefore on MakerWorld as a remix and not in this repository.

## Other artwork

- **The lunch face** (`src/lunch_face.h`) was made from an image generated with Google Gemini, cropped and warped into two frames by a script.
- **The photos and the video** in `docs/blog/images` (`cover-4x3.jpg`, `photo-desk.jpg`, `demo-*.jpg`, `demo.mp4`) are mine, taken on 19 November 2026.
- **The 3D renders** (`product-*.jpg` and the `broll-*.mp4` clips, marked "3D render") are made in Blender by `docs/blog/render/render_product.py` from the dock's model files; the board, cable, keyboard, monitor and desk in them are modelled, and the screens are the firmware's own screenshots. The lighting maps and the wood texture are from [Poly Haven](https://polyhaven.com) (CC0). `render-7-print-pose.jpg` is a plain drawing of the print orientation by `docs/blog/render_dock.py`.
- **The screenshots** in this kit were read from the Buddy's own frame buffer; the three reaction pictures in the guide are frames of the animation files. The work pages show sample data (tasks, pull requests, meetings), painted in or produced by the firmware's demo mode, not anyone's real work. The pictures of the personal touches and of the four faces are screenshots from a real board too (`docs/images/board`, taken with the example names Sam and Pixel; the bouncing name is drawn by `docs/guide/persona_shots.py`), and the installer and app screenshots come from their simulated modes.
