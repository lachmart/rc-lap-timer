#include "raceclock.h"

void RaceClock::start(int64_t nowUs, int countdownMs, int laps, int minutes, const uint8_t* order, int nHeats,
                      int prepSec) {
  race = { RaceState::Countdown, nowUs + (int64_t)countdownMs * 1000, laps, minutes, {}, 0, 0,
           HeatState::Running, 0, prepSec > 0 ? prepSec : 0, 0, countdownMs };
  if (laps <= 0 || !order) return;
  for (int i = 0; i < nHeats && race.nHeats < MAX_DRIVERS; i++) {
    if (order[i]) race.order[race.nHeats++] = order[i];
  }
}

uint8_t RaceClock::heatDriver() const {
  return race.heat < race.nHeats ? race.order[race.heat] : 0;
}

bool RaceClock::heatLaps(int laps, int64_t nowUs) {
  if (race.state != RaceState::Running || race.heatState != HeatState::Running || !heatDriver() || laps < race.laps) {
    return false;
  }
  race.heat++;
  race.prevGoUs = race.heatGoUs;
  if (race.heat < race.nHeats) {                       // the next driver gets ready
    race.heatState = race.prepSec ? HeatState::Countdown : HeatState::Waiting;
    race.heatGoUs  = race.prepSec ? nowUs + (int64_t)race.prepSec * 1000000 : 0;
  }
  return true;
}

bool RaceClock::startHeat(int64_t nowUs) {
  if (race.state != RaceState::Running || race.heat >= race.nHeats || race.heatState == HeatState::Running) return false;
  int64_t go = nowUs + (int64_t)race.countdownMs * 1000;
  if (race.heatState == HeatState::Countdown && race.heatGoUs <= go) return false;   // already counting down
  race.heatState = HeatState::Countdown;
  race.heatGoUs  = go;
  return true;
}

void RaceClock::backHeat() {
  if (race.heat == 0) return;
  race.heat--;
  race.heatState = HeatState::Running;
  race.heatGoUs  = race.prevGoUs;
}

bool RaceClock::update(int64_t nowUs) {
  if (race.state == RaceState::Countdown && nowUs >= race.goUs) {
    race.state = RaceState::Running;
    return true;
  }
  if (race.state == RaceState::Running && race.heatState == HeatState::Countdown && nowUs >= race.heatGoUs) {
    race.heatState = HeatState::Running;              // the next driver's "go"
    return true;
  }
  return false;
}

bool RaceClock::counts(int64_t passUs) const {
  switch (race.state) {
    case RaceState::Countdown: return false;               // cars lining up, not racing yet
    case RaceState::Running:                               // and between heats: the next driver gets ready
      if (race.nHeats && race.heat >= race.nHeats) return false;   // all heats done: the race is over
      return passUs >= race.goUs && race.heatState == HeatState::Running && passUs >= race.heatGoUs;
    default:                   return true;                // training
  }
}
