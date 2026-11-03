# RC Lap Timer

Lap timer for RC cars: one car with drivers taking turns, or up to 5 (firmware: 7) cars at once with IR
transponders; training and races. ESP32 + laser beam gate + IR receiver + web app on a phone over Wi-Fi.
Multi-car design agreed with Martin: [docs/multi-car.md](docs/multi-car.md).
Hobby project. Communication, code comments, UI and commits in English.

## Status

- Firmware **compiles cleanly** (platform espressif32 8.0.0 pinned, Arduino core 3.3.12, WebSockets 2.7.3; partition table `min_spiffs.csv` = 1.9 MB app with OTA, flash ~64 %). **Never uploaded to a board yet.** Transponder firmware (`transponder/`, ATtiny202) builds separately.
- Hardware not yet assembled or tested. `DEF_DELTA` and `DEF_MIN_LAP` are estimates, tuned via the web UI.
- Unit-tested on the host: laps, beam, NEC + blink decoders, pass matching (`pio test -e native`), race standings (`node --test test/js/*.test.js`). Web UI verified against the mock server only.
- **Untested on hardware**: switching `WIFI_AP_STA` ↔ `WIFI_AP` while the AP runs (should keep AP clients), mDNS restart after a late STA connect, scan while phones are on the AP, disconnect reason codes (wrong password vs. not found), GPIO 2 LED, OTA upload, whether the GL5528 LDR is fast enough for fast passes (watch pass depth vs. speed), the whole IR path (TSOP on GPIO 33, ISR, real transponder timing and range beside the track, ATtiny202 TCA0 carrier + SerialUPDI with the CP2102), ZRound talking to our RiCino emulation.
- Development on a Mac, PlatformIO in VS Code (`pio run -t upload`, `pio device monitor`).

## Structure

- `src/` – firmware split into modules, each `.h` is the module's interface:
  - `main.cpp` – `setup()`/`loop()` only, wires modules together
  - `config.h` – fixed constants (pins, AP name, limits, defaults)
  - `settings.*` – Preferences load/save, global `settings` struct
  - `sensor.*` – the gate task (core 1, ~1 kHz): ADC → `Beam`, IR receiver edges (ISR on GPIO 33 → lock-free ring buffer) → `NecDecoder` + `BlinkDecoder`/`BlinkIds` → `PassMatcher` → FreeRTOS queue of `Passing`s for the main loop. LED.
  - `beam.*` – `Beam` class: detection (deviation from idle, hysteresis delta/2, idle EMA), noise (decaying peak hold of clear-beam deviation, delayed 64 samples and 300 ms hold-off after a break so the slow LDR's ramps don't count), pass depth (peak of last break), `beamSuggestDelta()` (between 2·noise+50 and 0.7·depth, -1 = too weak). No hardware deps, unit-tested.
  - `nec.*` – RiCino-compatible NEC decoder (edge timestamps in, 32-bit code out, ±30 % tolerance) + encoder (used by tests; the transponder has its own). `necTransponderId()`: 0x1000–0xFFFF in the low 16 bits.
  - `blink.*` – NE556 "blink" transponders: rate from 8 steady periods (±5 %), suppressed for 80 ms after a > 3 ms mark (an NEC header, whose zero bits would look like ~890 Hz). `BlinkIds` maps readings within ±6 % to learned rates (seeded from saved cars).
  - `passes.*` – `PassMatcher`: single mode = every beam break is car 0; multi = sightings cluster per id (gap < 250 ms, max 2 s), a finished cluster takes the nearest beam break within ±200 ms (precise) or its middle (imprecise); unclaimed beam breaks → `unidentified`.
  - `timing.*` – main loop: passings → `carsSlotFor()` → `lapsCrossing()`, ZRound hook, mode switch, race state (countdown → running; at "go" `lapsRestart` on all cars; passings before "go" ignored). Race standings/finish are NOT in firmware: `web/race.js`.
  - `laps.*` – session state per car slot (`MAX_CARS` 8, slot 0 = beam-only car): lap ring buffer shared by all cars (`Lap{endUs, ms, num, car, driver, deleted, precise}`), per-car lap in progress/undo, stats per `key` = driver id or `STATS_CAR|slot`. Behind `portMUX` on ESP32, no hardware deps (time passed in), unit-tested. `lapsRevision()` change counter.
  - `cars.*` – car slots: transponder id (`tx`, RiCino 0x1000+ or blink Hz), name, driver; learned on first sighting; Preferences namespace `cars` (`tx`, `names`, `drv` = newline lists by slot, drivers saved by name). A driver drives one car at a time. Main loop only.
  - `drivers.*` – driver list (names in Preferences namespace `drivers`, ids assigned at runtime). Main loop only.
  - `zround.*` – USB serial mode: debug log 115200 or ZRound decoder (RiCino protocol, 19200: `%C&`/`%A&`, `%I&`, `%F&`, `%L<id>,<ms>&`, `%T<ms>&` every 5 s). `LOGF()` in `log.h` is silent in ZRound mode.
  - `wlan.*` – AP, STA state machine, captive DNS, mDNS (not named `network.*`: on macOS' case-insensitive FS it would shadow the framework's `Network.h`)
  - `ota.*` – ArduinoOTA (port 3232, password `OTA_PASSWORD`, its own mDNS disabled because `wlan` runs mDNS); env `ota` in platformio.ini (`espota`, 192.168.4.1)
  - `web.*` – WebServer, WebSocket, JSON building (with escaping)
- `web/` – `index.html`, `style.css`, `app.js`, `race.js` (race standings, UMD so node can test it) as normal files. Embedded into the firmware by `board_build.embed_txtfiles`, accessed in `web.cpp` via `asm("_binary_web_<name>_start")` symbols and served at `/`, `/style.css`, `/app.js`. No LittleFS / uploadfs needed.
- `test/test_*/` – Unity tests, env `native` builds only the hardware-free modules (`build_src_filter`: laps, beam, nec, passes, blink). `test/js/` – node:test for `web/race.js`. Run `pio test -e native` (plain `pio test` runs nothing, `esp32dev` has `test_ignore = *`).
- `tools/mock_server.py` – stdlib-only Python fake of the firmware (HTTP 8080 + WS 81, simulated car(s) with false triggers, `--cars 5` for transponder cars, `--manual`, `POST /mock/cross[?tx=]`, `POST /mock/block?on=1|0`, fake Wi-Fi networks; password `wrong` fails). **Mirrors laps/cars/drivers/timing logic: keep it in sync when behaviour changes.**
- `transponder/` – separate PlatformIO project (atmelmegaavr, ATtiny202 @ 10 MHz, `serialupdi`): NEC frames with TCA0 38 kHz carrier on PA3, random 5–20 ms gap; id via `PLATFORMIO_BUILD_FLAGS="-DTRANSPONDER_ID=0x1001"`. Circuit + programming in its README.
- `docs/multi-car.md` – agreed multi-car design (gate beside the track, race rules).
- `platformio.ini` – `default_envs = esp32dev`; envs `esp32dev`, `ota` (extends esp32dev, Wi-Fi upload) and `native`
- `README.md` – wiring, uploading, usage, API
- Arduino IDE is not supported (PlatformIO only).

## Hardware

- ESP32 dev board, 30 pins, ESP32-D0WD-V3, 4 MB flash, USB-serial CH9102 ([Dratek 1581](https://dratek.cz/arduino-platforma/1581-esp-32s-esp32-esp8266-development-board-2.4ghz-dual-mode-wifi-bluetooth-antenna-module.html)). PlatformIO board `esp32dev`. GPIO 32 is broken out as D32 (left side, 10th from bottom). Onboard blue LED on GPIO 2.
- 650 nm laser shines across the track into an LDR inside a black tube
- Divider `3V3 ── LDR ──┬── GPIO 32`, `└── 10 kΩ ── GND`
- The sensor must be on ADC1 (GPIO 32–39); ADC2 does not work while Wi-Fi is on
- IR receiver TSOP38238 on GPIO 33 (D33, next to D32), beside the track at car height in a tube. The ISR reads `GPIO_IN1_REG`, so `IR_PIN` must be 32–39.
- Transponders: ATtiny202 + 78L05 + BC337 + wide-angle IR LED, powered via servo Y-cable (cars have no free receiver port). Martin has a CP2102 USB-serial adapter for SerialUPDI. Cars max ~20 km/h.

## Firmware architecture

- **Measurement** runs in a separate FreeRTOS task (`sensorTask`, core 1, ~1 kHz via `vTaskDelay(1)`), timestamps from `esp_timer_get_time()` (µs).
- Detection (in `Beam`) = deviation from the idle value (`|raw - base| > delta`), so wiring polarity doesn't matter. Hysteresis `delta/2`. The idle value is calibrated at startup and on request; outside of a pass it slowly tracks ambient light (EMA ~2 s).
- Each car's first pass only starts its timing; every subsequent pass records a lap for that car's driver. Passes shorter than `minLapMs` are discarded (counter `ignored`).
- **Session/laps**: ring of `MAX_LAPS` laps (`{endUs, ms, driver, deleted}`), stats per driver kept incrementally so they survive ring wrap. Overall best = min of driver bests. Corrections:
  - undo (one level): reverts the last pass; after a lap pass `lastCrossUs = prevCrossUs`, so the lap continues from the real previous pass
  - restart: drop the lap in progress; switching driver does the same (a lap must not mix drivers)
  - delete: lap stays in the ring marked `deleted`, numbering stable
  - join(n): merges laps n and n+1 if back to back (`lapsContinues`: n+1 started at n's `endUs`, ±1 ms rounding), same driver, both counted. Later laps renumber.
  - Removing from the middle of a full ring (`popAt`) pins `floorKept` so an already overwritten slot is never exposed again.
- Shared lap state is protected by a `portMUX` inside `laps.cpp`. The sensor task only bumps the revision counter; `webLoop()` notices the change and does the broadcast. API handlers set `stateDirty` for changes outside laps.
- **Wi-Fi**: AP `RC_Lap_Timer` always runs (192.168.4.1) + captive portal (DNSServer answers everything, `onNotFound` redirects foreign hosts). Home network (STA) is a non-blocking state machine in `wlanLoop()`: Connecting → Connected / Waiting; lost → Connecting; Waiting retries every `STA_RETRY` (30 s) but only while no station is on the AP, and an attempt aborts as soon as a phone joins the AP (channel scanning disturbs AP clients). mDNS `laptimer.local`, restarted on STA connect. State in JSON as `sta` (`off|connecting|connected|waiting`) + `staErr` (`wrong password|network not found|failed`, only in `waiting`, from the STA_DISCONNECTED event reason; our own `disconnect()` = ASSOC_LEAVE is ignored) + `hasPass`; RSSI goes in the `live` message. Settings changes call `wlanApply()`: no restart, AP keeps running. Network scan is async (`scanNetworks(true, …, 120 ms/channel)`), refused while connecting; STA mode is switched on only for the scan.
- **Communication**: WebSocket on port 81. Messages `{"t":"state",...}` (on connect and any change) and `{"t":"live",...}` (raw ADC, base, broken, cur; every 200 ms). When the WS drops, the page switches to HTTP polling of `/api/state` every 1 s and keeps reconnecting the WS. Page takes `?ws=<port>` for the mock.
- HTTP API: see README. Failed actions return 400.
- Settings in `Preferences` namespace `laptimer` (`minLap`, `delta`, `ssid`, `pass`); drivers in namespace `drivers` (`names`, newline-separated). Names: control chars stripped, ≤ 32 bytes UTF-8 (UI `maxlength=16`), no duplicates, max 8.
- Page: current lap computed locally (`cur` + `performance.now()`); beep (WebAudio) and voice (speechSynthesis, en-US) are opt-in toggles remembered in localStorage, unlocked on the first `pointerdown`; big view overlay (⛶, Fullscreen API where available); beam-blocked warning after 3 s broken; sensor tuning panel (no-trigger band, noise, depth, suggestion within ±20 % counts as fine); vibration; flash on new/best lap; laps much shorter than the driver's median (< 60 %) are marked "short?"; per-lap ⋯ menu with join/delete (no confirm dialogs, the menu is the second step).

## Decisions and alternatives considered

- **One car, drivers take turns**: a driver is selected in the UI, laps are attributed to them, leaderboard per session. No persistent session history / CSV export (explicitly not wanted).
- **Multi-car decided** (docs/multi-car.md): RiCino-compatible IR transponders (ATtiny202; ATtiny85 and ESP32-C3 rejected as too expensive, CH32V003 not sold in Czechia) + laser for the time (hybrid), gate beside the track, races with per-car start at its first crossing after "go", standard finish rule, anyone can control races, ZRound over USB optional. NE556 blink transponders also decoded as a no-programming fallback. Rejected: separate lanes, UHF RFID, BLE RSSI.

## Multi-car: existing systems (researched 2026-11-03)

Goal: be compatible with existing transponders and race software instead of inventing a protocol.

- **RiCino** (github.com/cdemetriadis/RiCino-Lap-Counter, GPL-3.0): transponder = ATtiny85 + IR LED, `IRremote sendNEC(id, 32)` at **38 kHz**, id `0x1000–0xFFFF`, repeated every ~10 ms after each frame. An NEC frame takes ~60–70 ms, so IR alone is coarse (±35 ms) and can miss fast cars (RicinoNext notes it). Receiver = TSOP 38 kHz + IRremote. Easy to decode on ESP32 (RMT).
  - **ZRound serial protocol** (from RiCino `Receiver.ino`), 19200 baud: PC→decoder `%C&` connect, `%I&` start race, `%F&` finish; decoder→PC `%A&` ack (sent repeatedly after `%C&`), `%T<ms hex>&` time sync every 5 s, `%L<id hex>,<ms hex>&` lap passing (ms since `%I&`). ZRound ≥ 1.15.
- **i-Lap** (commercial IR, Mini-Z): proprietary, high carrier (~455 kHz per forum guesses), 7-digit IDs. MultiRC_Transponder (github.com/hindenbugbite/MultiRC_Transponder, MIT) has emulation codes (6-byte with CRC), untested. Not worth targeting first.
- **Easylap / Robitronic** IR: commercial transponders exist (ET001, Mini-Z); IrDA-like 500 µs pulses (RicinoNext notes). Undocumented.
- **AMB RC3 / MyLaps RC4** (club standard): inductive loop ~5 MHz, not IR. **OpenStint** (openstint.org) reads RC3 + clones, RC4 Hybrid and RC4 with an RTL-SDR + wire loop (~$65) on a PC or Raspberry Pi; outputs to LapBeeps natively, bridges to ZRound and RCGTiming. Can't run on the ESP32. MyLaps P3 protocol reference: skoky/P3tools, ghidinelli/ambmylapstiming.
- **RCHourglass** (loop-based DIY, PSoC): emulates AMBrc serial and CANO; non-commercial license with share-alike terms, avoid copying code.
- Most likely plan: RiCino-compatible IR transponders for car ID + the existing laser for the timestamp (hybrid), and optionally speak the ZRound serial protocol over USB.
- **Doppler**: acoustic (engine sound) rejected as a timer due to accuracy (~±10–30 ms, throttle changes the frequency). Microwave radar (HB100 10.5 GHz, ~19.5 Hz per km/h) is a possible future extension as a **speed meter** on the straight, not as a timer. Martin is checking whether he has the module.
- **Sensor**: LDR GL5528 for now. It is slow (20–30 ms), comparable to how long a fast car blocks the beam; if pass depth drops at speed, switch to a phototransistor (BPW85 / L-53P3C / TEPT4400) in the same divider, no code change. Sampling is 1 kHz (1 ms resolution), fine for a phototransistor too.
- **Bluetooth** deliberately not used. Staying with Wi-Fi for iPhone compatibility without installing an app.
- Martin wants the phone to **stay on home Wi-Fi while racing**, hence the background STA reconnect. At a track without home Wi-Fi, a phone hotspot can be the "home network".

## Possible next steps

- Upload, bring up the HW, tune `delta`/`minLap`, verify the untested items in Status
- HB100 radar for speed
