# RC Lap Timer

Lap timer for an RC car, drivers take turns. ESP32 + laser beam gate + web app on a phone over Wi-Fi.
Hobby project. Communication, code comments, UI and commits in English.

## Status

- Firmware **compiles cleanly** (platform espressif32 8.0.0 pinned, Arduino core 3.3.12, WebSockets 2.7.3; flash ~88 % of the default 1.3 MB app partition, switch to a bigger partition table if it grows). **Never uploaded to a board yet.**
- Hardware not yet assembled or tested. `DEF_DELTA` and `DEF_MIN_LAP` are estimates, tuned via the web UI.
- Lap logic unit-tested on the host (`pio test -e native`). Web UI verified against the mock server only.
- **Untested on hardware**: switching `WIFI_AP_STA` ↔ `WIFI_AP` while the AP runs (should keep AP clients), mDNS restart after a late STA connect, GPIO 2 LED.
- Development on a Mac, PlatformIO in VS Code (`pio run -t upload`, `pio device monitor`).

## Structure

- `src/` – firmware split into modules, each `.h` is the module's interface:
  - `main.cpp` – `setup()`/`loop()` only, wires modules together
  - `config.h` – fixed constants (pins, AP name, limits, defaults)
  - `settings.*` – Preferences load/save, global `settings` struct
  - `sensor.*` – sampling task + crossing detection + beam LED, calls `lapsCrossing()`
  - `laps.*` – session state: lap ring buffer, per-driver stats, undo/restart/delete/join. Behind `portMUX` on ESP32, no lock and no hardware deps on the host (time is passed in), so it's unit-tested. `lapsRevision()` change counter.
  - `drivers.*` – driver list (names in Preferences namespace `drivers`, ids assigned at runtime), selection; main loop only
  - `wlan.*` – AP, STA state machine, captive DNS, mDNS (not named `network.*`: on macOS' case-insensitive FS it would shadow the framework's `Network.h`)
  - `web.*` – WebServer, WebSocket, JSON building (with escaping), restart scheduling
- `web/` – `index.html`, `style.css`, `app.js` as normal files. Embedded into the firmware by `board_build.embed_txtfiles`, accessed in `web.cpp` via `asm("_binary_web_<name>_start")` symbols and served at `/`, `/style.css`, `/app.js`. No LittleFS / uploadfs needed.
- `test/test_laps/` – Unity tests for `laps.cpp`, env `native` builds only `laps.cpp` (`build_src_filter`).
- `tools/mock_server.py` – stdlib-only Python fake of the firmware (HTTP 8080 + WS 81, simulated car with false triggers, `--manual`, `POST /mock/cross`). **Mirrors laps/drivers logic: keep it in sync when behaviour changes.**
- `platformio.ini` – `default_envs = esp32dev`; envs `esp32dev` and `native`
- `README.md` – wiring, uploading, usage, API
- Arduino IDE is not supported (PlatformIO only).

## Hardware

- ESP32 dev board, 30 pins, ESP32-D0WD-V3, 4 MB flash, USB-serial CH9102 ([Dratek 1581](https://dratek.cz/arduino-platforma/1581-esp-32s-esp32-esp8266-development-board-2.4ghz-dual-mode-wifi-bluetooth-antenna-module.html)). PlatformIO board `esp32dev`. GPIO 32 is broken out as D32 (left side, 10th from bottom). Onboard blue LED on GPIO 2.
- 650 nm laser shines across the track into an LDR inside a black tube
- Divider `3V3 ── LDR ──┬── GPIO 32`, `└── 10 kΩ ── GND`
- The sensor must be on ADC1 (GPIO 32–39); ADC2 does not work while Wi-Fi is on

## Firmware architecture

- **Measurement** runs in a separate FreeRTOS task (`sensorTask`, core 1, ~1 kHz via `vTaskDelay(1)`), timestamps from `esp_timer_get_time()` (µs).
- Detection = deviation from the idle value (`|raw - base| > delta`), so wiring polarity doesn't matter. Hysteresis `delta/2`. The idle value is calibrated at startup and on request; outside of a pass it slowly tracks ambient light (EMA ~2 s).
- The first pass only starts timing; every subsequent pass records a lap for the selected driver. Passes shorter than `minLapMs` are discarded (counter `ignored`).
- **Session/laps**: ring of `MAX_LAPS` laps (`{endUs, ms, driver, deleted}`), stats per driver kept incrementally so they survive ring wrap. Overall best = min of driver bests. Corrections:
  - undo (one level): reverts the last pass; after a lap pass `lastCrossUs = prevCrossUs`, so the lap continues from the real previous pass
  - restart: drop the lap in progress; switching driver does the same (a lap must not mix drivers)
  - delete: lap stays in the ring marked `deleted`, numbering stable
  - join(n): merges laps n and n+1 if back to back (`lapsContinues`: n+1 started at n's `endUs`, ±1 ms rounding), same driver, both counted. Later laps renumber.
  - Removing from the middle of a full ring (`popAt`) pins `floorKept` so an already overwritten slot is never exposed again.
- Shared lap state is protected by a `portMUX` inside `laps.cpp`. The sensor task only bumps the revision counter; `webLoop()` notices the change and does the broadcast. API handlers set `stateDirty` for changes outside laps.
- **Wi-Fi**: AP `RC_Lap_Timer` always runs (192.168.4.1) + captive portal (DNSServer answers everything, `onNotFound` redirects foreign hosts). Home network (STA) is a non-blocking state machine in `wlanLoop()`: Connecting → Connected / Waiting; lost → Connecting; Waiting retries every `STA_RETRY` (30 s) but only while no station is on the AP, and an attempt aborts as soon as a phone joins the AP (channel scanning disturbs AP clients). mDNS `laptimer.local`, restarted on STA connect. State in JSON as `sta` (`off|connecting|connected|waiting`).
- **Communication**: WebSocket on port 81. Messages `{"t":"state",...}` (on connect and any change) and `{"t":"live",...}` (raw ADC, base, broken, cur; every 200 ms). When the WS drops, the page switches to HTTP polling of `/api/state` every 1 s and keeps reconnecting the WS. Page takes `?ws=<port>` for the mock.
- HTTP API: see README. Failed actions return 400.
- Settings in `Preferences` namespace `laptimer` (`minLap`, `delta`, `ssid`, `pass`); drivers in namespace `drivers` (`names`, newline-separated). Names: control chars stripped, ≤ 32 bytes UTF-8 (UI `maxlength=16`), no duplicates, max 8.
- Page: current lap computed locally (`cur` + `performance.now()`); beep (WebAudio) and voice (speechSynthesis, en-US) are opt-in toggles (iOS needs a tap); vibration; flash on new/best lap; laps much shorter than the driver's median (< 60 %) are marked "short?"; per-lap ⋯ menu with join/delete (no confirm dialogs, the menu is the second step).

## Decisions and alternatives considered

- **One car, drivers take turns**: a driver is selected in the UI, laps are attributed to them, leaderboard per session. No persistent session history / CSV export (explicitly not wanted).
- For multiple cars at once (not planned): separate lanes each with its own gate, or gates at different heights; with car modifications IR LEDs + NE555 at different frequencies and TSOP receivers. RFID (UHF as used at marathons) is expensive and doesn't like carbon/metal.
- **Doppler**: acoustic (engine sound) rejected as a timer due to accuracy (~±10–30 ms, throttle changes the frequency). Microwave radar (HB100 10.5 GHz, ~19.5 Hz per km/h) is a possible future extension as a **speed meter** on the straight, not as a timer. Martin is checking whether he has the module.
- **Bluetooth** deliberately not used. Staying with Wi-Fi for iPhone compatibility without installing an app.
- Martin wants the phone to **stay on home Wi-Fi while racing**, hence the background STA reconnect. At a track without home Wi-Fi, a phone hotspot can be the "home network".

## Possible next steps

- Upload, bring up the HW, tune `delta`/`minLap`, verify the untested items in Status
- HB100 radar for speed
