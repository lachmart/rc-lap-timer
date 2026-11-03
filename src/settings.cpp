#include "settings.h"
#include <Preferences.h>
#include "config.h"

Settings settings;
static Preferences prefs;

void settingsLoad() {
  prefs.begin("laptimer", false);
  settings.minLapMs = prefs.getUInt("minLap", DEF_MIN_LAP);
  settings.delta    = prefs.getInt("delta", DEF_DELTA);
  settings.ssid     = prefs.getString("ssid", "");
  settings.pass     = prefs.getString("pass", "");
  settings.multi    = prefs.getBool("multi", false);
  settings.zround   = prefs.getBool("zround", false);
  settings.raceLaps = prefs.getUShort("raceLaps", 10);
  settings.raceMin  = prefs.getUShort("raceMin", 0);
}

void settingsSaveTiming(long minLapMs, long delta) {
  settings.minLapMs = constrain(minLapMs, 300L, 600000L);
  settings.delta    = constrain(delta, 50L, 4000L);
  prefs.putUInt("minLap", settings.minLapMs);
  prefs.putInt("delta", settings.delta);
}

void settingsSaveWifi(const String& ssid, const String& pass) {
  String s = ssid;
  s.trim();
  settings.ssid = s;
  settings.pass = pass;
  prefs.putString("ssid", s);
  prefs.putString("pass", pass);
}

void settingsSaveMode(bool multi, bool zround) {
  settings.multi = multi;
  settings.zround = zround;
  prefs.putBool("multi", multi);
  prefs.putBool("zround", zround);
}

void settingsSaveRace(int laps, int minutes) {
  settings.raceLaps = constrain(laps, 0, 999);
  settings.raceMin  = constrain(minutes, 0, 600);
  prefs.putUShort("raceLaps", settings.raceLaps);
  prefs.putUShort("raceMin", settings.raceMin);
}
