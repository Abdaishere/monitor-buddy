# Pictures used by the README and the guide

| Folder or file | What it holds |
| --- | --- |
| `board/` | Screenshots read from a real Buddy over USB (`@MB SHOT`, 320×172): every mood and status face in the four styles (`STYLE_NAME.png`, taken with the example names Sam and Pixel) and five page pictures (`page_*.png`). |
| `faces.png`, `faces/`, `personal.png` | Sheets made from `board/` by `python docs/guide/persona_shots.py`. The bouncing name in `personal.png` is drawn by that script. |
| `setup/` | Screenshots of the browser installer and the laptop app, taken in their demo modes (no personal data). |

To take new board screenshots: plug the Buddy in, stop the helper (`systemctl --user stop buddy-link`), show a face with `@MB STATE {"demo": N, "page": 0}` and read the screen with `@MB SHOT` (see "Talk to the Buddy over USB" in [HANDOVER.md](../HANDOVER.md)).
