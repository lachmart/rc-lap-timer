// Drivers who take turns with the car. Names are saved in flash (Preferences, namespace "drivers"),
// ids are assigned at runtime. Only used from the main loop (web), no locking.
#pragma once
#include <Arduino.h>

struct Driver {
  uint8_t id;
  String  name;
};

void          driversLoad();                      // restores saved names, selects the first driver
int           driversCount();
const Driver& driversAt(int i);
uint8_t       driversSelected();                  // 0 = nobody
uint8_t       driversAdd(const String& name);     // returns the new id, 0 if full, empty or duplicate
bool          driversRemove(uint8_t id);
bool          driversSelect(uint8_t id);          // switching driver drops the lap in progress
