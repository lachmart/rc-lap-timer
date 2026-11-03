# Multi-car timing: design

Agreed with Martin on 2026-11-03. Up to 5 cars on track at once (firmware allows 7 + the beam-only car).

## Hardware

**Gate, beside the track** (not an arch):

- Laser + light sensor across the track as today. Gives the precise time (~1 ms).
- IR receiver TSOP38238 (38 kHz) next to the laser, at car height, in a short tube aimed across the
  track. It identifies the car. GPIO 33.
- Cars passing side by side can hide each other's transponder. Then the hidden car's passing is
  counted from IR alone (less precise) or missed. Accepted.

**Transponder per car** (~€1.2): ATtiny202 + 78L05 + BC337 + wide-angle IR LED (±25° or more,
e.g. TSAL6400) + 47 Ω, powered from the car receiver through a servo Y-cable (no free port).
LED on the side of the car facing the gate, pointing sideways and slightly up.
Programmed once with a CP2102 USB-serial adapter + 4.7 kΩ (SerialUPDI). Firmware in `transponder/`.
Frames repeat with a random 5–20 ms gap (not fixed), so two transponders near the gate don't keep
colliding in step.

**Protocol: RiCino-compatible**: NEC IR code at 38 kHz, 32 bits MSB first, id 0x1000–0xFFFF in the
low 16 bits, repeated every ~10 ms after each ~68 ms frame. Other people's RiCino transponders work
with our timer and ours with RiCino receivers. (The firmware also understands NE556 "blink"
transponders, a no-programming fallback, see `src/blink.h`.)

Numbers at 20 km/h (5.6 m/s, max speed of the cars): a 30 cm car blocks the beam ~50 ms, the LDR is
fast enough. One IR frame takes ~70 ms = ~40 cm of travel, so the receiver should see ~1 m of track.

## Timing

- Beam break + IR sightings → passing (`src/passes.h`): a car's sightings form a cluster; the beam
  break nearest the cluster's middle gives the precise time. No beam break → IR-only time, marked
  imprecise. Beam break nobody claims → "unidentified" (person, car without transponder).
- Each car has its own lap in progress, lap list, undo/restart; delete and join per lap (`src/laps.h`).
- Cars are learned automatically: an unknown transponder id becomes a new car. Name and driver are
  set in the app and saved.
- Single car without a transponder keeps working exactly as before (beam only, "car 0").

## Training and races

| | Training | Race |
|---|---|---|
| Start | each car's first crossing starts its timing | countdown in the app (beep + voice "3, 2, 1, go"); crossings before "go" are ignored; after "go" **each car's first crossing starts its timing** (no lap yet) |
| During | free laps | live standings: laps (more is better), then the time of the last crossing (earlier is better) |
| End | "New session" | after N laps or M minutes: when the leader completes the last lap (or the first crossing of the leader after the time is up), the race is finished and **every other car finishes at its next crossing** |
| Result | best laps per driver | finishing order, laps, total time (first crossing → finish), best lap, gap |

- Everyone with the page open can start, stop or reset a race. No confirmations beyond what exists.
- Corrections (undo, join, delete) work during and after a race; standings follow.

## ZRound (optional)

The timer can act as a ZRound decoder over USB, using RiCino's serial protocol (19200 baud):
`%C&` → `%A&`, `%I&` start, `%F&` finish, `%L<id hex>,<ms hex>&` passing, `%T<ms hex>&` every 5 s.
ZRound then runs the race itself; the app keeps showing the passings. In this mode the USB port
carries only the protocol, no debug log.
