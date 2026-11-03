// Race phases: off → countdown → running. Heats (one car, drivers in turn) have their own phases
// between drivers: after a heat the race waits for the next driver (a manual start, or prep seconds),
// then counts down; crossings before that driver's "go" don't count. No hardware dependencies
// (time passed in), unit-tested (test/test_raceclock). timing.cpp owns the one RaceClock.
#pragma once
#include <stdint.h>
#include "config.h"

enum class RaceState : uint8_t { Off, Countdown, Running };
enum class HeatState : uint8_t { Running, Waiting, Countdown };   // Waiting: for a manual start

struct RaceInfo {
  RaceState state;
  int64_t   goUs;      // when the countdown ends (esp_timer µs)
  int       laps;      // race length in laps, 0 = time race
  int       minutes;   // race length in minutes, 0 = laps race
  // Heats (one car, laps race): the drivers in turn, each drives `laps` laps; nHeats 0 = no heats.
  uint8_t   order[MAX_DRIVERS];
  int       nHeats;
  int       heat;      // index of the driver on the track; nHeats = all heats done
  HeatState heatState;
  int64_t   heatGoUs;  // "go" of the current heat (Running) or the coming one (Countdown); 0 = race go
  int       prepSec;   // time for the next driver to get ready, then the countdown; 0 = manual start
  int64_t   prevGoUs;  // heatGoUs of the heat before, for undo
  int       countdownMs;
};

class RaceClock {
 public:
  void start(int64_t nowUs, int countdownMs, int laps, int minutes,
             const uint8_t* order = nullptr, int nHeats = 0, int prepSec = 0);   // heats need laps > 0
  void stop() { race.state = RaceState::Off; }
  bool update(int64_t nowUs);            // true once at each "go" (the race's, and each heat's)
  bool counts(int64_t passUs) const;     // a passing at passUs is timed (not during/before the countdown)
  RaceInfo info() const { return race; }
  uint8_t heatDriver() const;            // driver of the current heat, 0 = no heats or all done
  bool heatLaps(int laps, int64_t nowUs);   // the current heat's driver has `laps` counted laps;
                                         // true when that ends the heat (next driver up or all done)
  bool startHeat(int64_t nowUs);         // the next driver is ready: count down now
  void skipHeat() { if (race.heat < race.nHeats) race.heat++; }
  void backHeat();                       // the pass that ended a heat was undone: that heat runs on

 private:
  RaceInfo race = { RaceState::Off, 0, 0, 0, {}, 0, 0, HeatState::Running, 0, 0, 0, 0 };
};
