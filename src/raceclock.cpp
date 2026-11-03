#include "raceclock.h"

void RaceClock::start(int64_t nowUs, int countdownMs, int laps, int minutes) {
  race = { RaceState::Countdown, nowUs + (int64_t)countdownMs * 1000, laps, minutes };
}

bool RaceClock::update(int64_t nowUs) {
  if (race.state != RaceState::Countdown || nowUs < race.goUs) return false;
  race.state = RaceState::Running;
  return true;
}

bool RaceClock::counts(int64_t passUs) const {
  switch (race.state) {
    case RaceState::Countdown: return false;               // cars lining up, not racing yet
    case RaceState::Running:   return passUs >= race.goUs;
    default:                   return true;                // training
  }
}
