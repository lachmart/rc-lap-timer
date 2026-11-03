// Race phases: off → countdown → running. No hardware dependencies (time passed in), unit-tested
// (test/test_raceclock). timing.cpp owns the one RaceClock and does what the phases mean.
#pragma once
#include <stdint.h>
#include "config.h"

enum class RaceState : uint8_t { Off, Countdown, Running };

struct RaceInfo {
  RaceState state;
  int64_t   goUs;      // when the countdown ends (esp_timer µs)
  int       laps;      // race length in laps, 0 = time race
  int       minutes;   // race length in minutes, 0 = laps race
  // Heats (one car, laps race): the drivers in turn, each drives `laps` laps; nHeats 0 = no heats.
  uint8_t   order[MAX_DRIVERS];
  int       nHeats;
  int       heat;      // index of the driver on the track; nHeats = all heats done
};

class RaceClock {
 public:
  void start(int64_t nowUs, int countdownMs, int laps, int minutes,
             const uint8_t* order = nullptr, int nHeats = 0);   // heats need laps > 0
  void stop() { race.state = RaceState::Off; }
  bool update(int64_t nowUs);            // true once, when the countdown has just ended ("go")
  bool counts(int64_t passUs) const;     // a passing at passUs is timed (not during/before the countdown)
  RaceInfo info() const { return race; }
  uint8_t heatDriver() const;            // driver of the current heat, 0 = no heats or all done
  bool heatLaps(int laps);               // the current heat's driver has `laps` counted laps;
                                         // true when that ends the heat (next driver up or all done)
  void skipHeat() { if (race.heat < race.nHeats) race.heat++; }

 private:
  RaceInfo race = { RaceState::Off, 0, 0, 0, {}, 0, 0 };
};
