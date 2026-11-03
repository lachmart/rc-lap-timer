// Lap bookkeeping for one session: laps, per-driver stats, corrections.
// Called from both the sensor task and the web, guarded by a lock internally.
// Has no hardware dependencies (time is passed in), so it is unit-tested on the host (test/).
#pragma once
#include <stdint.h>
#include "config.h"

constexpr int MAX_STATS = MAX_DRIVERS + 1;   // + driver 0 = "nobody selected"

struct Lap {
  int64_t  endUs;                // when the lap ended (esp_timer µs)
  uint32_t ms;
  uint8_t  driver;               // driver id, 0 = none
  bool     deleted;              // removed by the user, not counted anywhere
};

struct DriverStats {
  uint8_t  id;
  int      count;
  uint32_t bestMs;
  uint32_t totalMs;
};

struct LapSnapshot {
  bool        running;           // timing a lap (the start pass has happened)
  long        curMs;             // time of the current lap
  uint8_t     driver;            // currently selected driver
  bool        canUndo;           // the last pass can be undone
  int         count;             // total number of laps incl. deleted (may exceed MAX_LAPS)
  uint32_t    bestMs;            // best lap of the session
  uint32_t    ignored;           // passes discarded by the minimum lap time
  int         n;                 // how many laps are in the laps array
  Lap         laps[MAX_LAPS];    // last n laps, oldest first; lap number = count - n + i + 1
  int         nStats;
  DriverStats stats[MAX_STATS];
};

void     lapsCrossing(int64_t nowUs, uint32_t minLapMs);   // beam crossing
void     lapsReset();                    // new session, keeps the selected driver
void     lapsSetDriver(uint8_t id);      // a different driver drops the lap in progress
void     lapsForgetDriver(uint8_t id);   // driver removed, drop their stats
bool     lapsUndo();                     // undo the last pass (false trigger), lap continues from the previous one
void     lapsRestart();                  // drop the lap in progress, next pass starts timing
bool     lapsDelete(int number);         // delete lap by number (1-based)
bool     lapsJoin(int number);           // a false trigger split a lap: merge lap number with the next one
bool     lapsContinues(const Lap& prev, const Lap& next);   // next started exactly where prev ended
void     lapsSnapshot(LapSnapshot& out, int64_t nowUs);
long     lapsCurrentMs(int64_t nowUs);
uint32_t lapsRevision();                 // changes on every update, the web uses it to broadcast state
