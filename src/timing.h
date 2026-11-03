// Ties the gate to the session: passings → cars → laps, single/multi-car mode, races.
// Main loop only.
//
// Race (docs/multi-car.md): starting one begins a new session and a countdown; passings before
// "go" are ignored; at "go" every car's timing restarts, so each car's first crossing starts
// its timing. Standings and the finish are computed by the app from the laps (web/race.js),
// so corrections (undo, join, delete) are always reflected.
#pragma once
#include <stdint.h>

enum class RaceState : uint8_t { Off, Countdown, Running };

struct RaceInfo {
  RaceState state;
  int64_t   goUs;      // when the countdown ends (esp_timer µs)
  int       laps;      // race length in laps, 0 = time race
  int       minutes;   // race length in minutes, 0 = laps race
};

void     timingBegin();
void     timingLoop();
void     timingSetMulti(bool multi);
void     timingNewSession();                   // clears laps, ends a race
void     timingRaceStart(int laps, int minutes);
void     timingRaceStop();                     // back to training, laps stay
RaceInfo timingRace();
