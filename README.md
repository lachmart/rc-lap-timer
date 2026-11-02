# RC Lap Timer

Lap timer for a single RC car. ESP32 + laser beam gate + web app on a phone over Wi-Fi.

## Parts

- ESP32 dev board (ESP32-WROOM, "esp32dev")
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

## Project layout

```
platformio.ini     build config, embeds web/ files into the firmware
src/
  main.cpp         setup() / loop(), wires the modules together
  config.h         fixed constants (pin, AP name, defaults)
  settings.*       user settings in flash (Preferences)
  sensor.*         LDR sampling task, crossing detection
  laps.*           lap bookkeeping (thread-safe)
  wlan.*           Wi-Fi AP + captive portal, optional home Wi-Fi, mDNS
  web.*            HTTP server, API, WebSocket
web/
  index.html       the phone app
  style.css
  app.js
```

The files in `web/` are embedded into the firmware at build time (`board_build.embed_txtfiles`),
so one `upload` is all you need; there's no separate filesystem image.

## Upload

```
pio run -t upload
pio device monitor
```

## Usage

1. Turn on the laser, aim it into the LDR tube, then power the ESP32 (it calibrates idle at startup).
2. On the phone, connect to the **RC_Lap_Timer** Wi-Fi. The timer page opens by itself
   (captive portal). If it doesn't, open `http://192.168.4.1`.
3. The first pass starts timing, every following pass records a lap.

The **Sensor** section shows the live ADC value (helps with aiming the laser), calibration,
minimum lap time and sensitivity. Settings are saved to flash.

### Home Wi-Fi (optional)

In the **Wi-Fi** section you can enter your home network. After a restart the ESP32 also connects
to it and is reachable at `http://laptimer.local` (or the IP from the serial log). The phone then
doesn't need to switch Wi-Fi and keeps its internet. The RC_Lap_Timer network always runs. If the
home network isn't in range within 10 s at startup (e.g. outside at the track), only the own network runs.

Note: `.local` addresses work on iPhone and desktop, not on some Androids; use the IP address there.

### Instant times

The page gets data over WebSocket (port 81). A new lap arrives right after the pass, live sensor
data 5× per second. If the connection drops, the page switches to fallback mode (HTTP every second)
and reconnects the WebSocket. The dot in the top right shows the connection state.

## API

| Method | Path | Description |
|---|---|---|
| GET | `/api/state` | full state (laps, best, settings) |
| POST | `/api/reset` | new session |
| POST | `/api/calibrate` | calibrate idle |
| POST | `/api/config?minLap=2000&delta=800` | settings (ms, ADC) |
| POST | `/api/wifi?ssid=...&pass=...` | home Wi-Fi, empty ssid = forget |
| WS | `ws://<ip>:81/` | messages `{"t":"state",...}` and `{"t":"live",...}` |
