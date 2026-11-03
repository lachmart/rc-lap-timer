// Ties the gate to the session: passings → cars → laps, single/multi-car mode, races.
// Main loop only.
//
// Race (docs/multi-car.md): starting one begins a new session and a countdown; passings before
// "go" are ignored; at "go" every car's timing restarts, so each car's first crossing starts
// its timing. Standings and the finish are computed by the app from the laps (web/race.js),
// so corrections (undo, join, delete) are always reflected.
#pragma once
#include <stdint.h>
#include "raceclock.h"

void     timingBegin();
void     timingLoop();
void     timingSetMulti(bool multi);
bool     timingUndo(int car);                  // lapsUndo(), and in heats also undoes the handover it caused
void     timingNewSession();                   // clears laps, ends a race
void     timingRaceStart(int laps, int minutes, const uint8_t* order = nullptr, int nHeats = 0,
                         int prepSec = 0);     // heats: prep time between drivers, 0 = manual start
bool     timingHeatStart();                    // heats: the next driver is ready, count down now
void     timingRaceStop();                     // back to training, laps stay
RaceInfo timingRace();
