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
}

void settingsSaveTiming(long minLapMs, long delta) {
  settings.minLapMs = constrain(minLapMs, 300L, 600000L);
  settings.delta    = constrain(delta, 50L, 4000L);
  prefs.putUInt("minLap", settings.minLapMs);
  prefs.putInt("delta", settings.delta);
}

void settingsSaveWifi(const String& ssid, const String& pass) {
  String s = ssid;
  s.replace("\"", ""); s.replace("\\", "");          // so it can't break the state JSON
  s.trim();
  settings.ssid = s;
  settings.pass = pass;
  prefs.putString("ssid", s);
  prefs.putString("pass", pass);
}
