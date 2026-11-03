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
  sensor.*         LDR sampling task, beam LED
  beam.*           beam break detection, noise / pass depth, suggested sensitivity (unit-tested)
  laps.*           laps, per-driver stats, corrections (no hardware deps, unit-tested)
  drivers.*        driver names in flash, who's driving
  wlan.*           Wi-Fi AP + captive portal, home Wi-Fi in the background, mDNS
  web.*            HTTP server, API, WebSocket
web/
  index.html       the phone app
  style.css
  app.js
test/              unit tests for laps.cpp and beam.cpp (run on the Mac)
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
pio test -e native                                  # unit tests of lap and beam logic
python3 tools/mock_server.py --drivers Martin,Jana  # fake timer with a simulated car
```

The mock serves `web/` straight from disk (edit, reload), fakes the whole API and WebSocket,
and drives a simulated car with an occasional false trigger. Open `http://localhost:8080`,
or on the phone the address it prints. `--manual` turns the car off; trigger passes with
`curl -X POST localhost:8080/mock/cross`. `POST /mock/block?on=1` holds the beam broken.

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
("Jana, 8.13, personal best"). Both switches are remembered on each phone; browsers only allow sound
after the first tap on the page.

### Big view

**⛶** next to the connection dot opens a full-screen view with large digits (current lap, last,
best, driver), readable from a distance with the phone propped up. Tap to close.

### Sensor and tuning

The **Sensor** section shows the live ADC value with the no-trigger band around idle (helps with
aiming the laser), calibration, minimum lap time and sensitivity. Settings are saved to flash.

To find the right sensitivity, drive (or wave a hand) through the beam once. The section then shows
the **noise** (how much the signal wanders with a clear beam), the **pass depth** (how far a pass moves
it) and a **suggested** sensitivity safely in between; **Use suggested sensitivity** applies it. "Too weak"
means a pass barely differs from the noise: aim the laser better, shade the LDR from the sun.

If the beam stays broken for more than 3 s (laser knocked off aim, something in the gate), a warning
appears: no laps are counted until the beam is clear again.

### Home Wi-Fi (optional)

Join a home network so the phone can stay on its own Wi-Fi with internet. In the **Wi-Fi** section,
tap **Scan networks**, pick yours, enter the password and **Connect**. Changes apply right away,
the timer doesn't restart and the RC_Lap_Timer network keeps running. The timer is then at
`http://laptimer.local` (or the IP shown in the Wi-Fi section).

The section shows the state at a glance (also in its title when collapsed): connected with signal
bars, connecting, or why it failed (wrong password, network not found). Leave the password empty
to keep the saved one. The ESP32 only sees 2.4 GHz networks.

If the home network is out of range or drops, the timer retries every 30 s, but only while no phone is
on RC_Lap_Timer (searching for the router disturbs that network). Tip: at the track, set a phone
hotspot as the home network.

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
| POST | `/api/wifi?ssid=...&pass=...` | join a home network, applied without restart; no `pass` = keep the saved one; empty ssid = forget |
| POST | `/api/wifi/scan` | start a network scan (400 while connecting) |
| GET | `/api/wifi/scan` | `{"scanning":false,"networks":[{"ssid","rssi","open"}]}` |
| WS | `ws://<ip>:81/` | messages `{"t":"state",...}` and `{"t":"live",...}` (raw, base, broken, noise, peak, suggest, rssi, cur) |

Failed actions (unknown lap, duplicate name, …) answer `400 {"ok":false}`.
In the state, each lap is `[ms, driverId, deleted, continuesPrevious]`, oldest first.
