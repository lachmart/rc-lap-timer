# Transponder

One per car. ATtiny202 + IR LED, about €1.2. Sends a RiCino-compatible id (NEC code, 38 kHz), so it
also works with RiCino receivers and ZRound.

## Parts

| Part | Notes |
|---|---|
| ATtiny202 (SOIC-8) | SMD, 1.27 mm pitch; hand-solderable, or on a SOIC-8 → DIP adapter |
| 78L05 (TO-92) | powers the ATtiny; its input survives any receiver voltage (up to 30 V) |
| BC337 or S8050 (NPN) | switches the LED |
| IR LED 940 nm, wide angle (±25° or more, e.g. TSAL6400) | |
| 47 Ω | LED current ~90–130 mA pulses at 5–7.4 V |
| 1 kΩ | base resistor |
| 100 nF + 1 µF | regulator input / output |
| servo Y-cable | no free receiver port needed: plug it between the receiver and a servo or the ESC |

## Circuit

```
receiver +  ──┬── 78L05 IN                 78L05 OUT ──┬── ATtiny202 pin 1 (VCC)
              │                                        │
            100 nF                                    1 µF
              │                                        │
receiver −  ──┴── 78L05 GND ── ATtiny202 pin 8 (GND) ──┴── BC337 emitter

receiver +  ── IR LED anode,  IR LED cathode ── 47 Ω ── BC337 collector
ATtiny202 pin 7 (PA3) ── 1 kΩ ── BC337 base
```

BC337 pinout (flat side facing you, legs down): collector, base, emitter.

ATtiny202 pins: 1 VCC, 6 UPDI (PA0, programming), 7 PA3 (LED), 8 GND.

The LED runs straight from the receiver supply through the transistor, so the regulator only feeds
the chip. Average draw ~10 mA.

## Mounting

Beside-the-track gate: LED on the side of the car that faces the gate (cars always pass the same way),
pointing sideways and slightly up, at about the height of the IR receiver. Keep it uncovered (clear
or no body shell over it).

## Programming (once per car)

CP2102 USB-serial adapter as SerialUPDI programmer:

```
CP2102 TXD ── 4.7 kΩ ──┬── ATtiny202 pin 6 (UPDI)
CP2102 RXD ────────────┘
CP2102 GND ─────────────── pin 8 (GND)
CP2102 5V  ─────────────── pin 1 (VCC)   (programming only, transponder unplugged from the car)
```

Then, with a different id for every car (0x1000–0xFFFF):

```
cd transponder
ls /dev/cu.*                           # find the CP2102, e.g. /dev/cu.SLAB_USBtoUART or /dev/cu.usbserial-0001
PLATFORMIO_BUILD_FLAGS="-DTRANSPONDER_ID=0x1001" pio run -t upload --upload-port /dev/cu.SLAB_USBtoUART
```

Write the id on the transponder. In the app (Cars section, multi-car mode) the car appears as `#1001`
the first time it passes the gate; give it a name and a driver.

**Check before mounting:** point the LED at the gate's IR receiver; the Cars section shows
"last transponder #1001, just now". A phone camera also shows the IR LED glowing faintly.
