// Race phases: off → countdown → running. No hardware dependencies (time passed in), unit-tested
// (test/test_raceclock). timing.cpp owns the one RaceClock and does what the phases mean.
#pragma once
#include <stdint.h>

enum class RaceState : uint8_t { Off, Countdown, Running };

struct RaceInfo {
  RaceState state;
  int64_t   goUs;      // when the countdown ends (esp_timer µs)
  int       laps;      // race length in laps, 0 = time race
  int       minutes;   // race length in minutes, 0 = laps race
};

class RaceClock {
 public:
  void start(int64_t nowUs, int countdownMs, int laps, int minutes);
  void stop() { race.state = RaceState::Off; }
  bool update(int64_t nowUs);            // true once, when the countdown has just ended ("go")
  bool counts(int64_t passUs) const;     // a passing at passUs is timed (not during/before the countdown)
  RaceInfo info() const { return race; }

 private:
  RaceInfo race = { RaceState::Off, 0, 0, 0 };
};
