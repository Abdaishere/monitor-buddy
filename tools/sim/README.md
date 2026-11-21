# Monitor Buddy simulator

The real firmware (`src/main.cpp` and `lib/`) built for a PC, on a simulated
Waveshare ESP32-C6-Touch-LCD-1.47: the ST7789 panel, touch controller, motion
sensor, BOOT button, flash, SD card, Wi-Fi, clock and USB serial are emulated
in `host/`. Time is simulated, so a whole working day runs in seconds and the
same script always gives the same pixels.

    make test                 # build, then run every scenario (needs g++, python3, pytest, Pillow, cryptography)

The first build fetches the libraries into `.deps/` (`fetch_deps.sh`).

| command | what it does |
|---|---|
| `make` | builds `.build/mbsim` |
| `make test` | runs the scenarios in `tests/`; screenshots are compared to the pixel with `golden/*.png` |
| `MBSIM_UPDATE_GOLDEN=1 make test` | rewrites the golden pictures (look at the diff before committing) |
| `python3 workday.py OUT` | films a simulated working day: `workday.mp4`, a contact sheet and a timeline |

Results land in `out/`: `results.json` (pass or fail per scenario), `shots/`
(every screenshot), `diffs/` (want, got and difference for a failed picture).

## Writing a scenario

    from mbsim import Buddy
    with Buddy() as buddy:              # boots on Monday 2026-01-05, 08:00 Dubai
        buddy.run(12)                   # simulated seconds
        buddy.state(locked=False, mic=True)
        buddy.double_knock()
        buddy.shot().save('on-air.png')

`mbsim.py` has the whole vocabulary: fingers (`tap`, `swipe_left`, `hold`),
the motion sensor (`knock`, `lay_flat`, `shake`), Wi-Fi and internet up and
down, the card, `@MB` commands over USB, the laptop's packets over the link,
restarts and power cycles. `mock_internet.py` answers the weather, stock,
coin, GitHub and update requests. `tests/conftest.py` has the fixtures.

## The whole workday

`workday.py` plays Monday 2026-01-05 from power-on at 07:55 to Tuesday 08:02:
the laptop's feed every minute, the morning briefing, a knock, a focus session
laid flat, Claude Code, CI red and green, two meetings, lunch, away and the
screensaver, a shake, the eighth hour, the end of the day, the night window
and the next morning's briefing. `tests/test_workday.py` (marker `workday`,
some minutes) compares each of its 48 moments with `golden/workday_*.png`;
`python3 workday.py OUT` (`make workday`) films them: `workday.mp4`,
`workday-contact-sheet.png`, `workday-timeline.md`.

    python3 -m pytest tests -q -m "not workday"     # the quick scenarios only

## The browser installer against the firmware

`tests_web/` opens `web/index.html` in a real Chromium (Playwright). Its
`navigator.serial` is `tests_web/fake_serial.js`: one USB port whose other end
is the simulator, so the page's `@MB` lines are read by the real firmware code
and the page reads the firmware's real answers: connect, read the settings,
change them (the Buddy restarts), a new Wi-Fi, a wrong password, backup,
reset, a silent board, a phone-sized window. Requests to the internet are
answered inside the test; nothing reaches a real board. Flashing is not
covered (that needs the chip's ROM loader; `web-tests/flasher.test.mjs` tests
those steps against a mock).

    pip install playwright && playwright install chromium
    make web                       # or: python3 -m pytest tests_web -q

Screenshots of the pages land in `out/web/`.

## The app's live preview

The app's editor (`tools/buddy_app.py`, "Make it yours") shows the Buddy's
screen while you change its settings. That screen is this simulator, built for
the browser: `build_wasm.sh` compiles the same `src/main.cpp`, `lib/` and
`host/` with Emscripten into `tools/app/sim/mbsim.js` and `mbsim.wasm` (both
committed, so the app needs no compiler). In a browser the tasks are
Emscripten fibers instead of `ucontext`, HTTP goes to a mock internet in
JavaScript, and `host/sim_tz.cpp` reads the POSIX time-zone rule (a browser's
C library knows only the computer's zone). `tools/app/sim/buddy-sim.js` drives
it: what `mbsim.py` is for the tests, in JavaScript.

    ./build_wasm.sh                              # needs emcc on the PATH, or Docker (emscripten/emsdk)
    python3 -m pytest tests/test_preview.py      # needs node: the browser build against the golden pictures
    python3 -m pytest tests_web/test_app.py      # the editor in Chromium, the native simulator on its cable
    MBSIM_BROWSER=firefox python3 -m pytest tests_web/test_app.py    # the same in Firefox (playwright install firefox)

Build it again after a change to anything the screen shows. `test_preview.py`
runs the same script on the browser build as `test_pages.py` and
`test_faces.py` run natively and compares with the same golden pictures, to
the pixel.

## CI

`.github/workflows/sim.yml` builds the simulator and runs the scenarios, the
workday, the browser tests and a build of three other panel sizes on every
push and pull request. A golden picture that differs fails the run and the
want/got/diff pictures are kept as an artifact. The golden pictures are the same with g++ 13 (Ubuntu 24.04, the runner) and
g++ 15; if a compiler ever rounds a float another way, look at the diff
pictures before rewriting them.
