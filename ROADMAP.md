# Roadmap

Monitor Buddy is a free hobby project, so this is a wish list, not a promise. Everything here was left out of 1.0 on purpose: it could not be finished, tested and seen on a real screen in time, and a half-done feature on a 1.47-inch display is worse than none.

## Next update

| Idea | Why it waited |
|---|---|
| **Arabic** (and other right-to-left languages) | The screen fonts are Latin only. Arabic needs letter shaping, right-to-left layout and a font that stays readable at this size; shipping it unverified would mean broken text. Needs a shaping step in the tools and a look at the device. |
| **More accent colours** | 1.0 has the five themes of the installer. A free colour picker needs a contrast check for every page first. |
| **Per-day work hours and custom days off** | 1.0 has one start, end and lunch hour, and four weekend choices. |
| **Reminders without the hour grid** | The water and lunch reminders follow the clock (on the hour). An interval of your own, and "not during a meeting" for every reminder, come later. |
| **The laptop app for other systems** (Flatpak, Fedora, Arch) | 1.0 has a .deb for Ubuntu and Debian and a ZIP for the rest. |
| **The personal touches in the installer's live preview** | The preview shows the pages and themes. The face styles are shown as pictures the firmware drew; captions are described in words. |
| **Face styles for the lunch face and the ON AIR sign** | Those two are full-screen pictures and look the same in every style. |
| **A switch for the AbdA page in the installer** | Today the author's signature page can only be hidden from the laptop app's page list. Planned for 1.0.1, with the last em dashes in the firmware's serial messages. |
| **More face styles** | 1.0 has four. A new one is a few drawing rules in `src/main.cpp` and a run of the simulator's face scenarios. |

## Ideas without a date

- Windows and macOS versions of the laptop helper.
- More languages for the installer page.

Want one of these sooner? Open an issue, or better, a pull request.

Other displays: not planned. Monitor Buddy is drawn for the 320x172 panel of the Waveshare ESP32-C6-Touch-LCD-1.47.
