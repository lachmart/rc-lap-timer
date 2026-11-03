#include "timing.h"
#include "esp_timer.h"
#include "cars.h"
#include "config.h"
#include "laps.h"
#include "raceclock.h"
#include "sensor.h"
#include "settings.h"
#include "zround.h"

static RaceClock race;

void timingBegin() { sensorSetMulti(settings.multi); }

void timingLoop() {
  if (race.update(esp_timer_get_time())) {
    for (int s = 0; s < MAX_CARS; s++) lapsRestart(s);   // each car's first crossing after "go" starts it
  }

  Passing p;
  while (sensorNextPassing(p)) {
    if (!race.counts(p.tUs)) continue;
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
  race.stop();
  lapsReset();
  sensorResetCounters();
}

void timingRaceStart(int laps, int minutes) {
  settingsSaveRace(laps, minutes);
  lapsReset();
  sensorResetCounters();
  race.start(esp_timer_get_time(), RACE_COUNTDOWN_MS, settings.raceLaps, settings.raceMin);
}

void timingRaceStop() { race.stop(); }

RaceInfo timingRace() { return race.info(); }
