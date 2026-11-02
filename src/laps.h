// Lap bookkeeping. Called from both the sensor task and the web, guarded by a lock internally.
#pragma once
#include <stdint.h>
#include "config.h"

struct LapSnapshot {
  bool     running;              // the start pass has happened
  long     curMs;                // time of the current lap
  int      count;                // total number of laps (may exceed MAX_LAPS)
  uint32_t bestMs;
  uint32_t ignored;              // passes discarded by the minimum lap time
  int      n;                    // how many laps are in the laps array
  uint32_t laps[MAX_LAPS];       // last n laps, oldest first
};

void     lapsCrossing(int64_t nowUs, uint32_t minLapMs);   // beam crossing
void     lapsReset();
void     lapsSnapshot(LapSnapshot& out);
long     lapsCurrentMs();
uint32_t lapsRevision();         // changes on every update, the web uses it to broadcast state
