// Cars on the track and who drives them. Slot 0 is the beam-only car (single-car mode),
// slots 1..MAX_CARS-1 are transponder cars, learned automatically the first time their
// transponder is seen. Saved in flash (Preferences, namespace "cars"). Main loop only.
#pragma once
#include <Arduino.h>

struct CarInfo {
  uint16_t tx;       // transponder id: RiCino 0x1000+, NE556 blink rate in Hz (< 0x1000); 0 = slot free
  String   name;     // optional, the app shows the transponder id otherwise
};

void           carsLoad();                          // after driversLoad()
bool           carsUsed(int slot);                  // slot 0 always is
const CarInfo& carsAt(int slot);
int            carsSlotFor(uint16_t tx);            // known car, or learn a new one; -1 if all slots are taken
int            carsKnownBlink(uint16_t* out, int max);   // saved blink transponders, for the decoder
bool           carsRename(int slot, const String& name);
bool           carsRemove(int slot);                // its laps stay in the session
bool           carsSetDriver(int slot, uint8_t driver, bool handOver = false);  // 0 = nobody; a driver drives
                                                    // one car at a time; handOver: see lapsSetDriver()
void           carsDriverAdded(uint8_t driver);     // the first driver takes the beam-only car
void           carsDriverRemoved(uint8_t driver);
