#include "cars.h"
#include <Preferences.h>
#include "config.h"
#include "drivers.h"
#include "laps.h"

static Preferences prefs;
static CarInfo     cars[MAX_CARS];

// Saved as three newline-separated lists indexed by slot: transponder ids (hex), names, driver names.
static void save() {
  String tx, names, drv;
  for (int s = 0; s < MAX_CARS; s++) {
    if (s) { tx += '\n'; names += '\n'; drv += '\n'; }
    tx += String(cars[s].tx, HEX);
    names += cars[s].name;
    uint8_t d = lapsDriver(s);
    for (int i = 0; i < driversCount(); i++) if (driversAt(i).id == d) drv += driversAt(i).name;
  }
  prefs.putString("tx", tx);
  prefs.putString("names", names);
  prefs.putString("drv", drv);
}

static String line(const String& all, int index) {
  int start = 0;
  for (int i = 0; i < index; i++) {
    start = all.indexOf('\n', start);
    if (start < 0) return "";
    start++;
  }
  int end = all.indexOf('\n', start);
  return all.substring(start, end < 0 ? all.length() : end);
}

void carsLoad() {
  prefs.begin("cars", false);
  String tx = prefs.getString("tx", ""), names = prefs.getString("names", ""), drv = prefs.getString("drv", "");
  for (int s = 0; s < MAX_CARS; s++) {
    cars[s].tx   = s ? (uint16_t)strtoul(line(tx, s).c_str(), nullptr, 16) : 0;
    cars[s].name = line(names, s);
    lapsSetDriver(s, driversFind(line(drv, s)));
  }
  if (lapsDriver(0) == 0 && driversCount() > 0) carsDriverAdded(driversAt(0).id);   // first start after an update
}

bool           carsUsed(int slot) { return slot == 0 || (slot > 0 && slot < MAX_CARS && cars[slot].tx != 0); }
const CarInfo& carsAt(int slot)   { return cars[slot]; }

int carsSlotFor(uint16_t tx) {
  if (tx == 0) return 0;
  for (int s = 1; s < MAX_CARS; s++) if (cars[s].tx == tx) return s;
  for (int s = 1; s < MAX_CARS; s++) {
    if (cars[s].tx == 0) {
      cars[s] = { tx, "" };
      lapsSetDriver(s, 0);
      save();
      return s;
    }
  }
  return -1;
}

int carsKnownBlink(uint16_t* out, int max) {
  int n = 0;
  for (int s = 1; s < MAX_CARS && n < max; s++) if (cars[s].tx && cars[s].tx < 0x1000) out[n++] = cars[s].tx;
  return n;
}

bool carsRename(int slot, const String& name) {
  if (!carsUsed(slot)) return false;
  cars[slot].name = driversCleanName(name);
  save();
  return true;
}

bool carsRemove(int slot) {
  if (slot <= 0 || !carsUsed(slot)) return false;
  lapsSetDriver(slot, 0);
  cars[slot] = { 0, "" };
  save();
  return true;
}

bool carsSetDriver(int slot, uint8_t driver) {
  if (!carsUsed(slot) || (driver && !driversExists(driver))) return false;
  if (driver) {
    for (int s = 0; s < MAX_CARS; s++) if (s != slot && lapsDriver(s) == driver) lapsSetDriver(s, 0);
  }
  lapsSetDriver(slot, driver);
  save();
  return true;
}

void carsDriverAdded(uint8_t driver) {
  if (lapsDriver(0) == 0) carsSetDriver(0, driver);
}

void carsDriverRemoved(uint8_t driver) {
  for (int s = 0; s < MAX_CARS; s++) if (lapsDriver(s) == driver) lapsSetDriver(s, 0);
  if (lapsDriver(0) == 0) {                           // the beam-only car takes the next free driver
    for (int i = 0; i < driversCount(); i++) {
      uint8_t d = driversAt(i).id;
      bool busy = false;
      for (int s = 1; s < MAX_CARS; s++) busy |= lapsDriver(s) == d;
      if (!busy) { lapsSetDriver(0, d); break; }
    }
  }
  save();
}
