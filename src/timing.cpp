#include "timing.h"
#include "esp_timer.h"
#include "cars.h"
#include "config.h"
#include "laps.h"
#include "sensor.h"
#include "settings.h"
#include "zround.h"

static RaceInfo race = { RaceState::Off, 0, 0, 0 };

void timingBegin() { sensorSetMulti(settings.multi); }

void timingLoop() {
  int64_t now = esp_timer_get_time();
  if (race.state == RaceState::Countdown && now >= race.goUs) {
    race.state = RaceState::Running;
    for (int s = 0; s < MAX_CARS; s++) lapsRestart(s);   // each car's first crossing after "go" starts it
  }

  Passing p;
  while (sensorNextPassing(p)) {
    if (race.state == RaceState::Countdown) continue;      // cars lining up, not racing yet
    if (race.state == RaceState::Running && p.tUs < race.goUs) continue;
    zroundPassing(p.id, p.tUs);
    int slot = p.id ? carsSlotFor(p.id) : (settings.multi ? -1 : 0);
    if (slot >= 0) lapsCrossing(slot, p.tUs, settings.minLapMs, p.precise);
  }
}

void timingSetMulti(bool multi) {
  settingsSaveMode(multi, settings.zround);
  sensorSetMulti(multi);
}

void timingNewSession() {
  race.state = RaceState::Off;
  lapsReset();
  sensorResetCounters();
}

void timingRaceStart(int laps, int minutes) {
  settingsSaveRace(laps, minutes);
  lapsReset();
  sensorResetCounters();
  race = { RaceState::Countdown, esp_timer_get_time() + (int64_t)RACE_COUNTDOWN_MS * 1000,
           settings.raceLaps, settings.raceMin };
}

void timingRaceStop() { race.state = RaceState::Off; }

RaceInfo timingRace() { return race; }
