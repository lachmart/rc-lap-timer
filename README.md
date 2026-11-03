# RC Lap Timer

Lap timer for an RC car with drivers taking turns. ESP32 + laser beam gate + web app on a phone over Wi-Fi.

## Parts

- ESP32 dev board, 30 pins (ESP32-WROOM, "esp32dev", CH9102 USB-serial)
- 650 nm laser module (5 V or 3.3 V)
- LDR photoresistor (e.g. GL5528)
- 10 kΩ resistor
- black tube 2–3 cm long over the LDR (shields ambient light)

## Wiring

```
3V3 ── LDR ──┬── GPIO 32
             └── 10 kΩ ── GND

Laser  VCC → 5V (or 3V3), GND → GND
```

On the 30-pin DevKit board: **D32** is on the left side, 10th pin from the bottom; **3V3** is the bottom-right pin;
**VIN** (≈5 V from USB, for the laser) is the bottom-left pin; **GND** is 2nd from the bottom on both sides.

The laser can also have its own power supply on the other side of the track, so no wires cross the track.
The sensor must be on ADC1 (GPIO 32–39); ADC2 doesn't work while Wi-Fi is running.

Wiring polarity doesn't matter. The code watches the deviation from the idle value, not an absolute threshold.
The board's blue LED (GPIO 2) lights while the beam is broken, which helps with aiming.

## Project layout

```
platformio.ini     build config, embeds web/ files into the firmware, native test env
src/
  main.cpp         setup() / loop(), wires the modules together
  config.h         fixed constants (pins, AP name, limits, defaults)
  settings.*       user settings in flash (Preferences)
  sensor.*         LDR sampling task, crossing detection, beam LED
  laps.*           laps, per-driver stats, corrections (no hardware deps, unit-tested)
  drivers.*        driver names in flash, who's driving
  wlan.*           Wi-Fi AP + captive portal, home Wi-Fi in the background, mDNS
  web.*            HTTP server, API, WebSocket
web/
  index.html       the phone app
  style.css
  app.js
test/test_laps/    unit tests for laps.cpp (run on the Mac)
tools/
  mock_server.py   fake timer for trying the app without the board
```

The files in `web/` are embedded into the firmware at build time (`board_build.embed_txtfiles`),
so one `upload` is all you need; there's no separate filesystem image.

## Upload

```
pio run -t upload
pio device monitor
```

If the upload hangs at `Connecting...`, hold the **BOOT** button until it starts.
If it breaks off halfway, lower `upload_speed` in `platformio.ini` to 460800.

## Develop without the board

```
pio test -e native                                  # unit tests of the lap logic
python3 tools/mock_server.py --drivers Martin,Jana  # fake timer with a simulated car
```

The mock serves `web/` straight from disk (edit, reload), fakes the whole API and WebSocket,
and drives a simulated car with an occasional false trigger. Open `http://localhost:8080`,
or on the phone the address it prints. `--manual` turns the car off; trigger passes with
`curl -X POST localhost:8080/mock/cross`.

## Usage

1. Turn on the laser, aim it into the LDR tube, then power the ESP32 (it calibrates idle at startup).
2. On the phone, connect to the **RC_Lap_Timer** Wi-Fi. The timer page opens by itself
   (captive portal). If it doesn't, open `http://192.168.4.1`.
3. The first pass starts timing, every following pass records a lap.

### Drivers and leaderboard

Add drivers under **Edit drivers** (up to 8, names are saved). Tap a name under **Driving now** when
the controller changes hands. Laps are recorded for that driver and the **Leaderboard** ranks drivers
by best lap, with gap, lap count and average. Switching driver drops the lap in progress; the next pass
starts the new driver's lap. **New session** clears laps and the leaderboard, drivers stay.

Without any drivers it works as a plain lap timer.

### Fixing false passes

| Situation | Fix |
|---|---|
| Something broke the beam mid-lap, you notice right away | **↶ Undo pass**: the false pass is removed, the lap continues from the real start |
| You notice later: one real lap shows as two short ones | tap **⋯** on the lap → **Join with lap N** |
| A lap that shouldn't count (crash, cut the track) | **⋯** → **Delete lap** (stays in the list crossed out, not counted) |
| False start, car stopped mid-lap | **⟲ Restart lap**: the next pass starts timing again |

Laps much shorter than the driver's usual lap are marked **short?**, typically a false trigger to join.
Passes shorter than the minimum lap time (Sensor section) are discarded automatically.

### Sound and voice

**Beep** plays a tone and vibrates on every lap (higher for a best). **Voice** reads the driver and time
("Jana, 8.13, personal best"). On iPhone the switch must be tapped once after opening the page.

### Sensor

The **Sensor** section shows the live ADC value (helps with aiming the laser), calibration,
minimum lap time and sensitivity. Settings are saved to flash.

### Home Wi-Fi (optional)

In the **Wi-Fi** section you can enter a home network. The timer joins it in the background and is
reachable at `http://laptimer.local` (or the IP shown in the Wi-Fi section). The phone can stay on its
home Wi-Fi with internet. The RC_Lap_Timer network always runs as well.

If the home network is out of range or drops, the timer retries every 30 s, but only while no phone is
on RC_Lap_Timer (searching for the router disturbs that network). Tip: at the track, set a phone
hotspot as the "home network".

Note: `.local` addresses work on iPhone and desktop, not on some Androids; use the IP address there.

### Instant times

The page gets data over WebSocket (port 81). A new lap arrives right after the pass, live sensor
data 5× per second. If the connection drops, the page switches to fallback mode (HTTP every second)
and reconnects the WebSocket. The dot in the top right shows the connection state.

## API

| Method | Path | Description |
|---|---|---|
| GET | `/api/state` | full state (laps, drivers, stats, settings) |
| POST | `/api/reset` | new session |
| POST | `/api/undo` | undo the last pass |
| POST | `/api/restart` | drop the lap in progress |
| POST | `/api/lap/delete?n=5` | delete lap 5 |
| POST | `/api/lap/join?n=5` | join lap 5 with lap 6 |
| POST | `/api/driver/add?name=Jana` | add a driver |
| POST | `/api/driver/remove?id=2` | remove a driver |
| POST | `/api/driver/select?id=2` | who's driving now (0 = nobody) |
| POST | `/api/calibrate` | calibrate idle |
| POST | `/api/config?minLap=2000&delta=800` | settings (ms, ADC) |
| POST | `/api/wifi?ssid=...&pass=...` | home Wi-Fi, empty ssid = forget; restarts |
| WS | `ws://<ip>:81/` | messages `{"t":"state",...}` and `{"t":"live",...}` |

Failed actions (unknown lap, duplicate name, …) answer `400 {"ok":false}`.
In the state, each lap is `[ms, driverId, deleted, continuesPrevious]`, oldest first.
