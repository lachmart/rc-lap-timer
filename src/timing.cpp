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

// Heats: hand the car to the next driver right after the last driver's heat (so undoing that pass
// gives it back). Their timing starts at the car's first crossing after their own "go".
static void nextHeat() {
  while (race.heatDriver() && !carsSetDriver(0, race.heatDriver(), true)) {
    race.skipHeat();                                  // driver removed meanwhile
  }
}

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
    if (slot < 0) continue;
    lapsCrossing(slot, p.tUs, settings.minLapMs, p.precise);
    if (slot == 0 && race.heatDriver() &&
        race.heatLaps(lapsCountOf(0, race.heatDriver(), race.info().goUs), p.tUs)) nextHeat();
  }
}

void timingSetMulti(bool multi) {
  settingsSaveMode(multi, settings.zround);
  sensorSetMulti(multi);
}

bool timingUndo(int car) {
  uint8_t before = lapsDriver(car);
  if (!lapsUndo(car)) return false;
  if (car == 0 && lapsDriver(0) != before) race.backHeat();   // undid a heat's last lap: the car is back
  return true;
}

void timingNewSession() {
  race.stop();
  lapsReset();
  sensorResetCounters();
}

void timingRaceStart(int laps, int minutes, const uint8_t* order, int nHeats, int prepSec) {
  settingsSaveRace(laps, minutes);
  lapsReset();
  sensorResetCounters();
  if (settings.multi) nHeats = 0;                     // heats are for one car with drivers taking turns
  race.start(esp_timer_get_time(), RACE_COUNTDOWN_MS, settings.raceLaps, settings.raceMin, order, nHeats,
             prepSec < 0 ? 0 : prepSec > 600 ? 600 : prepSec);
  nextHeat();
}

bool timingHeatStart() { return race.startHeat(esp_timer_get_time()); }

void timingRaceStop() { race.stop(); }

RaceInfo timingRace() { return race.info(); }
