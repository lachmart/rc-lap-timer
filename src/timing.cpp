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

// Heats: hand the car to the next driver. Their lap in progress is dropped, so their timing
// starts at the car's next crossing (one untimed lap to hand over the transmitter).
static void nextHeat() {
  while (race.heatDriver() && !carsSetDriver(0, race.heatDriver())) {
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
        race.heatLaps(lapsCountOf(0, race.heatDriver(), race.info().goUs))) nextHeat();
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

void timingRaceStart(int laps, int minutes, const uint8_t* order, int nHeats) {
  settingsSaveRace(laps, minutes);
  lapsReset();
  sensorResetCounters();
  if (settings.multi) nHeats = 0;                     // heats are for one car with drivers taking turns
  race.start(esp_timer_get_time(), RACE_COUNTDOWN_MS, settings.raceLaps, settings.raceMin, order, nHeats);
  nextHeat();
}

void timingRaceStop() { race.stop(); }

RaceInfo timingRace() { return race.info(); }
