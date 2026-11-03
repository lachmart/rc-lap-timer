// The gate: laser beam (LDR) + IR transponder receiver, sampled in a separate FreeRTOS task.
// Detection logic lives in beam.h, nec.h, blink.h and passes.h; this module is the hardware glue.
// Finished passings are queued for the main loop (sensorNextPassing).
#pragma once
#include <stdint.h>
#include "passes.h"

struct SensorLive {
  int      raw;          // latest ADC value
  int      base;         // idle value (beam on the sensor)
  bool     broken;       // the beam is currently interrupted
  int      noise;        // how far the signal wanders with a clear beam
  int      peak;         // depth of the last beam break
  int      suggest;      // suggested sensitivity, 0 = no pass yet, -1 = signal too weak
  uint16_t irId;         // last transponder seen, 0 = none yet
  long     irAgoMs;      // how long ago
  uint32_t unidentified; // beam breaks no transponder claimed (multi-car mode)
};

// knownBlink: saved NE556 transponder rates, so they keep their ids after a restart.
void       sensorBegin(const uint16_t* knownBlink, int nKnownBlink);
void       sensorCalibrate();               // request a new idle calibration
void       sensorSetMulti(bool multi);      // single car (beam only) or transponder cars
void       sensorResetCounters();
bool       sensorNextPassing(Passing& p);   // main loop: next finished passing, if any
SensorLive sensorLive();
