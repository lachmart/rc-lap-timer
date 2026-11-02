// User settings stored in flash (Preferences, namespace "laptimer")
#pragma once
#include <Arduino.h>

struct Settings {
  volatile uint32_t minLapMs;   // also read by the sensor task
  volatile int      delta;
  String            ssid;       // home Wi-Fi, empty = own AP only
  String            pass;
};

extern Settings settings;

void settingsLoad();
void settingsSaveTiming(long minLapMs, long delta);   // clamps values to a sane range
void settingsSaveWifi(const String& ssid, const String& pass);
