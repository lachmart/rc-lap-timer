// User settings stored in flash (Preferences, namespace "laptimer")
#pragma once
#include <Arduino.h>

struct Settings {
  volatile uint32_t minLapMs;   // also read by the sensor task
  volatile int      delta;
  String            ssid;       // home Wi-Fi, empty = own AP only
  String            pass;
  bool              multi;      // transponder cars instead of one beam-only car
  bool              zround;     // USB port speaks the ZRound decoder protocol instead of the debug log
  uint16_t          raceLaps;   // last race setup: laps (0 = time race)
  uint16_t          raceMin;    //                  minutes (0 = laps race)
};

extern Settings settings;

void settingsLoad();
void settingsSaveTiming(long minLapMs, long delta);   // clamps values to a sane range
void settingsSaveWifi(const String& ssid, const String& pass);
void settingsSaveMode(bool multi, bool zround);
void settingsSaveRace(int laps, int minutes);
