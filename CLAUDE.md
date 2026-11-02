# RC Lap Timer

Lap timer for an RC car. ESP32 + laser beam gate + web app on a phone over Wi-Fi.
Hobby project. Communication, code comments, UI and commits in English.

## Status

- Code **compiles cleanly** (2026-11-02: platform espressif32 8.0.0, Arduino core 3.3.12, WebSockets 2.7.3; flash ~86 % of the default 1.3 MB app partition). **Never uploaded to a board yet.**
- Hardware not yet assembled or tested. The values `DEF_DELTA` and `DEF_MIN_LAP` are estimates and are tuned via the web UI.
- Development on a Mac, PlatformIO in VS Code (`pio run -t upload`, `pio device monitor`).

## Structure

- `src/` – firmware split into modules, each `.h` is the module's interface:
  - `main.cpp` – `setup()`/`loop()` only, wires modules together
  - `config.h` – fixed constants (pin, AP name, defaults)
  - `settings.*` – Preferences load/save, global `settings` struct
  - `sensor.*` – sampling task + crossing detection, calls `lapsCrossing()`
  - `laps.*` – lap ring buffer behind `portMUX`, `lapsSnapshot()`, `lapsRevision()` change counter
  - `wlan.*` – AP, STA, captive DNS, mDNS (not named `network.*`: on macOS' case-insensitive FS it would shadow the framework's `Network.h`)
  - `web.*` – WebServer, WebSocket, JSON building, restart scheduling
- `web/` – `index.html`, `style.css`, `app.js` as normal files. Embedded into the firmware by `board_build.embed_txtfiles`, accessed in `web.cpp` via `asm("_binary_web_<name>_start")` symbols and served at `/`, `/style.css`, `/app.js`. No LittleFS / uploadfs needed.
- `platformio.ini` – standard layout (`src/`), lib `links2004/WebSockets`
- `README.md` – wiring, uploading, usage, API
- Arduino IDE is no longer supported (PlatformIO only), because of the module layout and embedded files.

## Hardware

- ESP32 dev board, 30 pins, ESP32-D0WD-V3, 4 MB flash, USB-serial CH9102 ([Dratek 1581](https://dratek.cz/arduino-platforma/1581-esp-32s-esp32-esp8266-development-board-2.4ghz-dual-mode-wifi-bluetooth-antenna-module.html)). PlatformIO board `esp32dev`. GPIO 32 is broken out as D32.
- 650 nm laser shines across the track into an LDR inside a black tube
- Divider `3V3 ── LDR ──┬── GPIO 32`, `└── 10 kΩ ── GND`
- The sensor must be on ADC1 (GPIO 32–39); ADC2 does not work while Wi-Fi is on

## Firmware architecture

- **Measurement** runs in a separate FreeRTOS task (`sensorTask`, core 1, ~1 kHz via `vTaskDelay(1)`), timestamps from `esp_timer_get_time()` (µs).
- Detection = deviation from the idle value (`|raw - base| > delta`), so wiring polarity doesn't matter. Hysteresis `delta/2`. The idle value is calibrated at startup and on request; outside of a pass it slowly tracks ambient light (EMA ~2 s).
- The first pass only starts timing; every subsequent pass records a lap. Passes shorter than `minLapMs` are discarded (counter `ignored`).
- Shared lap state is protected by a `portMUX` inside `laps.cpp`. The sensor task only bumps the revision counter; `webLoop()` notices the change and does the broadcast.
- **Wi-Fi**: AP `RC_Lap_Timer` always runs (192.168.4.1) + captive portal (DNSServer answers everything, `onNotFound` redirects foreign hosts). Optionally STA on a home network (SSID/password in Preferences, set from the UI). If STA fails to connect within 10 s, it is turned off so scanning doesn't disturb the AP. mDNS `laptimer.local`.
- **Communication**: WebSocket on port 81. Messages `{"t":"state",...}` (laps, best, settings; on connect, new lap, reset, config change) and `{"t":"live",...}` (raw ADC, base, broken, cur; every 200 ms). When the WS drops, the page switches to HTTP polling of `/api/state` every 1 s and keeps reconnecting the WS.
- HTTP API: `GET /api/state`, `POST /api/reset`, `/api/calibrate`, `/api/config?minLap=&delta=`, `/api/wifi?ssid=&pass=` (then restarts).
- Settings in `Preferences` namespace `laptimer` (`minLap`, `delta`, `ssid`, `pass`).
- Page: the current lap is computed locally on the client (`cur` + `performance.now()`), sound via WebAudio (enabled by a button), vibration, flash on a new/best lap.

## Decisions and alternatives considered

- **Only one car** for now. For multiple cars without modifying the cars, separate lanes each with its own gate, or gates at different heights, were considered. With car modifications: IR LEDs + NE555 at different frequencies and TSOP receivers. RFID (UHF as used at marathons) is expensive and doesn't like carbon/metal.
- **Doppler**: acoustic (engine sound) rejected as a timer due to accuracy (~±10–30 ms, throttle changes the frequency). Microwave radar (HB100 10.5 GHz, ~19.5 Hz per km/h) is a possible future extension as a **speed meter** on the straight, not as a timer. Martin is checking whether he has the module.
- **Bluetooth** deliberately not used. Staying with Wi-Fi for iPhone compatibility without installing an app.

## Possible next steps

- First compile, upload, bring up the HW, tune `delta`/`minLap`
- HB100 radar for speed
- Training history stored on the ESP32 + CSV export
- Voice announcement of times (speechSynthesis)
