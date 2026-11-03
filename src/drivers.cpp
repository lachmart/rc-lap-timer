#include "drivers.h"
#include <Preferences.h>
#include "config.h"
#include "cars.h"
#include "laps.h"

static Preferences prefs;
static Driver      list[MAX_DRIVERS];
static int         count    = 0;
static uint8_t     nextId   = 1;

static int indexOf(uint8_t id) {
  for (int i = 0; i < count; i++) if (list[i].id == id) return i;
  return -1;
}

static uint8_t newId() {
  for (;;) {
    uint8_t id = nextId++;
    if (nextId == 0) nextId = 1;
    if (id != 0 && indexOf(id) < 0) return id;
  }
}

// No control characters (names are stored newline-separated), trimmed, at most MAX_NAME bytes
// without cutting a UTF-8 character in half.
String driversCleanName(const String& in) {
  String s;
  for (unsigned i = 0; i < in.length(); i++) if ((uint8_t)in[i] >= 0x20) s += in[i];
  s.trim();
  if (s.length() > (unsigned)MAX_NAME) {
    unsigned cut = MAX_NAME;
    while (cut > 0 && ((uint8_t)s[cut] & 0xC0) == 0x80) cut--;
    s = s.substring(0, cut);
    s.trim();
  }
  return s;
}

static void save() {
  String all;
  for (int i = 0; i < count; i++) { if (i) all += '\n'; all += list[i].name; }
  prefs.putString("names", all);
}

void driversLoad() {
  prefs.begin("drivers", false);
  String all = prefs.getString("names", "");
  int start = 0;
  while (start <= (int)all.length() && count < MAX_DRIVERS) {
    int end = all.indexOf('\n', start);
    if (end < 0) end = all.length();
    String name = all.substring(start, end);
    if (name.length()) list[count++] = { newId(), name };
    start = end + 1;
  }
}

int           driversCount()      { return count; }
const Driver& driversAt(int i)    { return list[i]; }

uint8_t driversAdd(const String& name) {
  String n = driversCleanName(name);
  if (!n.length() || count >= MAX_DRIVERS) return 0;
  for (int i = 0; i < count; i++) if (list[i].name == n) return 0;

  uint8_t id = newId();
  list[count++] = { id, n };
  save();
  carsDriverAdded(id);
  return id;
}

bool driversRemove(uint8_t id) {
  int i = indexOf(id);
  if (i < 0) return false;
  for (; i < count - 1; i++) list[i] = list[i + 1];
  count--;
  save();
  lapsForgetDriver(id);
  carsDriverRemoved(id);
  return true;
}

uint8_t driversFind(const String& name) {
  for (int i = 0; i < count; i++) if (list[i].name == name) return list[i].id;
  return 0;
}

bool driversExists(uint8_t id) { return id != 0 && indexOf(id) >= 0; }
