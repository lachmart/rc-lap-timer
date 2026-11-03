# RC Lap Timer

Lap timer for RC cars: one car with drivers taking turns, or several cars at once with IR transponders.
Training and races. ESP32 + laser beam gate + IR receiver + web app on a phone over Wi-Fi.

## Parts

- ESP32 dev board, 30 pins (ESP32-WROOM, "esp32dev", CH9102 USB-serial)
- 650 nm laser module (5 V or 3.3 V)
- LDR photoresistor (e.g. GL5528)
- 10 kΩ resistor
- black tube 2–3 cm long over the LDR (shields ambient light)
- for several cars: IR receiver **TSOP38238** (38 kHz) + one [transponder](transponder/README.md) per car (~€1.2 each)

**LDR vs. phototransistor.** An LDR reacts slowly (GL5528: ~20–30 ms), and a fast car blocks the beam
for about as long (40 cm car at 50 km/h ≈ 30 ms). The LDR then only partly darkens: fast passes show
a smaller **pass depth** in the Sensor section and may be missed. If that happens, swap the LDR for a
phototransistor (e.g. BPW85, L-53P3C, TEPT4400), which reacts in microseconds: collector to 3V3,
emitter to GPIO 32, 10 kΩ from GPIO 32 to GND, same tube. No code change needed.
Aim the beam at the height of the car body, not between the wheels.

## Wiring

```
3V3 ── LDR ──┬── GPIO 32
             └── 10 kΩ ── GND

Laser  VCC → 5V (or 3V3), GND → GND

TSOP38238  OUT → GPIO 33, VS → 3V3, GND → GND      (only for several cars)
```

On the 30-pin DevKit board: **D32** is on the left side, 10th pin from the bottom; **3V3** is the bottom-right pin;
**VIN** (≈5 V from USB, for the laser) is the bottom-left pin; **GND** is 2nd from the bottom on both sides.

The laser can also have its own power supply on the other side of the track, so no wires cross the track.
The sensor must be on ADC1 (GPIO 32–39); ADC2 doesn't work while Wi-Fi is running.

Wiring polarity doesn't matter. The code watches the deviation from the idle value, not an absolute threshold.
The board's blue LED (GPIO 2) lights while the beam is broken, which helps with aiming.

**IR receiver (several cars).** D33 is right next to D32 (9th pin from the bottom on the left).
Mount the TSOP38238 next to the laser at car height, inside a short black tube aimed across the track, so it
only sees the cars around the finish line (about 1 m of track). Transponders go on the side of the car that
faces the gate. Details and the transponder circuit: [transponder/README.md](transponder/README.md),
design: [docs/multi-car.md](docs/multi-car.md).

## Project layout

```
platformio.ini     build config, embeds web/ files into the firmware, native test env
src/
  main.cpp         setup() / loop(), wires the modules together
  config.h         fixed constants (pins, AP name, limits, defaults)
  settings.*       user settings in flash (Preferences)
  sensor.*         the gate: LDR + IR receiver task, beam LED
  beam.*           beam break detection, noise / pass depth, suggested sensitivity (unit-tested)
  nec.*, blink.*   transponder decoders: RiCino NEC code, NE556 blink rate (unit-tested)
  passes.*         beam breaks + transponder sightings → passings (unit-tested)
  timing.*         passings → cars → laps, single/multi-car mode, races
  raceclock.*      race phases: countdown, go, which passings count, heats (unit-tested)
  laps.*           per-car laps, per-driver stats, corrections (unit-tested)
  cars.*           transponder cars (learned, saved), who drives which car
  drivers.*        driver names in flash
  zround.*         USB: debug log or ZRound decoder
  zroundproto.*    the ZRound/RiCino serial protocol (unit-tested)
  wlan.*           Wi-Fi AP + captive portal, home Wi-Fi in the background, mDNS
  ota.*            firmware upload over Wi-Fi
  web.*            HTTP server, API, WebSocket
  webjson.*        JSON messages for the app (unit-tested)
  lines.h          newline-separated lists in flash (cars, drivers)
web/
  index.html       the phone app
  style.css
  app.js           the client: state, rendering, actions
  race.js          race standings, lap entry fields (unit-tested with node)
  sound.js         beep, voice, vibration
  wifi.js          home Wi-Fi panel
  raceview.js      race view, race setup, big view
transponder/       ATtiny202 transponder firmware + circuit (separate PlatformIO project)
docs/multi-car.md  multi-car design
test/              unit tests (C++ for the firmware modules, JS for race.js, Python: mock matches firmware)
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
If it breaks off after `Changing baud rate`, lower `upload_speed` in `platformio.ini` (it is 460800, the CH9102 board fails at 921600).

**Over Wi-Fi** (after the first USB upload, e.g. with the timer mounted at the track): connect the laptop
to RC_Lap_Timer and run `pio run -e ota -t upload`. On the home network use
`pio run -e ota -t upload --upload-port laptimer.local`. The password is `OTA_PASSWORD` in
`src/config.h` (and `upload_flags` in `platformio.ini`); change both if you like.

## Develop without the board

```
pio test -e native                                  # firmware unit tests (laps, beam, decoders, passes, race, ZRound, JSON)
node --test test/js/*.test.js                       # race standings tests
python3 -m unittest discover -s test/py             # the mock sends the same JSON as the firmware
python3 tools/mock_server.py --drivers Martin,Jana  # fake timer with a simulated car
python3 tools/mock_server.py --cars 5 --drivers Martin,Jana,Tom,Eva,Petr   # five transponder cars
```

The mock serves `web/` straight from disk (edit, reload), fakes the whole API and WebSocket,
and drives a simulated car with an occasional false trigger. Open `http://localhost:8080`,
or on the phone the address it prints. `--manual` turns the car off; trigger passes with
`curl -X POST localhost:8080/mock/cross` (add `?tx=4097` for a transponder car). `POST /mock/block?on=1`
holds the beam broken.

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

### Several cars

In the **Cars** section switch on **Several cars with IR transponders**. Every transponder is learned
automatically the first time it passes the gate and shows up as a car (`#1001` for RiCino ids, `465 Hz` for
NE556 blink transponders). Give it a name there, and pick who drives it under **Drivers** (one dropdown per
car; a driver drives one car at a time). The **On track** table shows each car's lap, current time, last and
best lap; **⋯** on a row has Undo pass / Restart lap for that car.

The laser gives the precise time; the transponder only tells which car it was. A pass the beam didn't see
(cars side by side) is still counted from the transponder, marked **~** (±35 ms). Beam breaks without any
transponder near (a person, a car without one) are counted as "passes without a transponder".

### Races

Open **Race**, choose a number of laps or minutes and **Start race**. The session is cleared and a
countdown runs (3, 2, 1, go, also by voice). Crossings before "go" are ignored; each car's first crossing
after "go" starts its timing. Standings: more laps first, then whoever crossed the line earlier. The race
ends when the leader completes the last lap (or, in a time race, crosses the line after the time is up);
every other car finishes at its next crossing (🏁). **Stop race** / **Back to training** returns to
training, the laps stay. Undo, join and delete also work in a race; the standings follow.

**Heats (one car, drivers in turn):** with one car and a laps race, **Race** shows the drivers in turn; tap
to leave one out or change the order. Each driver drives the laps on their own: after a driver's last lap
the timer hands the car to the next one by itself (no tap needed), whose time starts at the car's next
crossing, so there is one untimed lap to hand over the transmitter. Fastest total time wins.

### ZRound

In **USB / ZRound** the timer becomes a ZRound decoder on the USB port (RiCino protocol, 19200 baud).
ZRound then runs its own races; the app keeps working. The USB port has no debug log in this mode.

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

**⛶** next to the connection dot opens a full-screen view with large digits, readable from a distance
with the phone propped up: current lap, last and best with one car; the cars table in training with
several cars; the countdown and standings in a race. Tap to close.

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
| GET | `/api/state` | full state (cars, laps, drivers, stats, race, settings) |
| POST | `/api/reset` | new session (also ends a race) |
| POST | `/api/undo?car=0` | undo the car's last pass (car = slot, 0 = the beam-only car) |
| POST | `/api/restart?car=0` | drop the car's lap in progress |
| POST | `/api/lap/delete?n=5` | delete session lap 5 |
| POST | `/api/lap/join?n=5` | join session lap 5 with the same car's next lap |
| POST | `/api/driver/add?name=Jana` | add a driver |
| POST | `/api/driver/remove?id=2` | remove a driver |
| POST | `/api/driver/select?id=2&car=0` | who drives the car (0 = nobody) |
| POST | `/api/car/rename?slot=1&name=Buggy` | name a transponder car |
| POST | `/api/car/remove?slot=1` | forget a transponder car |
| POST | `/api/mode?multi=1` | several cars with transponders (1) or one beam-only car (0) |
| POST | `/api/race/start?laps=10&minutes=0[&order=2,3,1]` | new session + countdown; laps or minutes; `order` = heats (one car, laps race): driver ids in turn |
| POST | `/api/race/stop` | back to training, laps stay |
| POST | `/api/usb?zround=1` | USB port: ZRound decoder (1) or debug log (0) |
| POST | `/api/calibrate` | calibrate idle |
| POST | `/api/config?minLap=2000&delta=800` | settings (ms, ADC) |
| POST | `/api/wifi?ssid=...&pass=...` | join a home network, applied without restart; no `pass` = keep the saved one; empty ssid = forget |
| POST | `/api/wifi/scan` | start a network scan (400 while connecting) |
| GET | `/api/wifi/scan` | `{"scanning":false,"networks":[{"ssid","rssi","open"}]}` |
| WS | `ws://<ip>:81/` | messages `{"t":"state",...}` and `{"t":"live",...}` (raw, base, broken, noise, peak, suggest, irId, irAgo, unident, rssi, cur per car slot) |

Failed actions (unknown lap, duplicate name, …) answer `400 {"ok":false}`.
In the state, each lap is `[ms, driverId, deleted, continuesPrevious, car, lapNumberOfCar, precise, endMs]`,
oldest first; `now` is the timer's clock in ms, so end times and `race.go` can be compared with it.
