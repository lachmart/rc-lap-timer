// Driver names, saved in flash (Preferences, namespace "drivers"); ids are assigned at runtime.
// Which driver drives which car is in cars.h. Only used from the main loop (web), no locking.
#pragma once
#include <Arduino.h>

struct Driver {
  uint8_t id;
  String  name;
};

void          driversLoad();                      // restores saved names
int           driversCount();
const Driver& driversAt(int i);
uint8_t       driversAdd(const String& name);     // returns the new id, 0 if full, empty or duplicate
bool          driversRemove(uint8_t id);
uint8_t       driversFind(const String& name);    // id by name, 0 if not found
bool          driversExists(uint8_t id);
String        driversCleanName(const String& name);  // trimmed, no control chars, at most MAX_NAME bytes
