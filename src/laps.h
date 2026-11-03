// Lap bookkeeping for one session: per-car timing, laps, per-driver stats, corrections.
// Called from the main loop (passings) and the web, guarded by a lock internally.
// Has no hardware dependencies (time is passed in), so it is unit-tested on the host (test/).
//
// Cars are slots 0..MAX_CARS-1. Slot 0 is the beam-only car used in single-car mode; transponder
// cars get other slots (see cars.h). Each car has its own lap in progress, lap numbers and undo.
#pragma once
#include <stdint.h>
#include "config.h"

constexpr int MAX_STATS = MAX_DRIVERS + MAX_CARS;   // per driver, plus per car for laps without a driver

struct Lap {
  int64_t  endUs;                // when the lap ended (esp_timer µs)
  uint32_t ms;
  uint16_t num;                  // lap number of this car
  uint8_t  car;                  // car slot
  uint8_t  driver;               // driver id, 0 = none
  bool     deleted;              // removed by the user, not counted anywhere
  bool     precise;              // timed by the beam (false = IR transponder only, ±35 ms)
};

// Stats key: the driver id, or STATS_CAR | slot for laps of a car without a driver.
constexpr uint16_t STATS_CAR = 0x100;
struct DriverStats {
  uint16_t key;
  int      count;
  uint32_t bestMs;
  uint32_t totalMs;
};

struct CarState {
  bool    running;               // timing a lap (the start pass has happened)
  long    curMs;                 // time of the lap in progress
  bool    canUndo;               // the last pass can be undone
  uint8_t driver;
  int     laps;                  // laps of this car incl. deleted
};

struct LapSnapshot {
  CarState    cars[MAX_CARS];
  int         count;             // total laps of all cars incl. deleted (may exceed MAX_LAPS)
  uint32_t    bestMs;            // best lap of the session
  uint32_t    ignored;           // passes discarded by the minimum lap time
  int         n;                 // how many laps are in the laps array
  Lap         laps[MAX_LAPS];    // last n laps, oldest first; session lap number = count - n + i + 1
  int         nStats;
  DriverStats stats[MAX_STATS];
};

void     lapsCrossing(int car, int64_t nowUs, uint32_t minLapMs, bool precise = true);
void     lapsReset();                          // new session, drivers stay with their cars
// A different driver drops that car's lap in progress. handOver (heats): the pass that just ended
// the previous driver's lap stays undoable, and undoing it gives the car back to them.
void     lapsSetDriver(int car, uint8_t id, bool handOver = false);
uint8_t  lapsDriver(int car);
void     lapsForgetDriver(uint8_t id);         // driver removed: drop their stats
bool     lapsUndo(int car);                    // undo the car's last pass (false trigger), lap continues from the previous one
void     lapsRestart(int car);                 // drop the car's lap in progress, its next pass starts timing
bool     lapsDelete(int seq);                  // delete lap by session lap number (1-based)
bool     lapsJoin(int seq);                    // a false trigger split a lap: merge it with the same car's next lap
bool     lapsContinues(const Lap& prev, const Lap& next);   // next started exactly where prev ended
int      lapsCountOf(int car, uint8_t driver, int64_t afterUs);   // counted laps ending after afterUs
void     lapsSnapshot(LapSnapshot& out, int64_t nowUs);
long     lapsCurrentMs(int car, int64_t nowUs);
uint32_t lapsRevision();                       // changes on every update, the web uses it to broadcast state
