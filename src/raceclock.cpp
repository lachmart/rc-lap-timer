#include "raceclock.h"

void RaceClock::start(int64_t nowUs, int countdownMs, int laps, int minutes, const uint8_t* order, int nHeats) {
  race = { RaceState::Countdown, nowUs + (int64_t)countdownMs * 1000, laps, minutes, {}, 0, 0 };
  if (laps <= 0 || !order) return;
  for (int i = 0; i < nHeats && race.nHeats < MAX_DRIVERS; i++) {
    if (order[i]) race.order[race.nHeats++] = order[i];
  }
}

uint8_t RaceClock::heatDriver() const {
  return race.heat < race.nHeats ? race.order[race.heat] : 0;
}

bool RaceClock::heatLaps(int laps) {
  if (race.state != RaceState::Running || !heatDriver() || laps < race.laps) return false;
  race.heat++;
  return true;
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
