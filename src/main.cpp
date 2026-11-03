// RC Lap Timer – ESP32 + laser beam gate + IR transponders (one car or several, training and races)
//
// A laser shines across the track into a photoresistor (LDR). A passing car breaks the beam,
// the ESP32 records the time and sends it to the phone over WebSocket.
//
// Modules
//  - settings  settings in flash (Preferences)
//  - sensor    the gate: LDR + IR receiver in a FreeRTOS task, beam LED
//  - beam      beam break detection, noise and pass depth for tuning
//  - nec/blink transponder decoders (RiCino NEC code, NE556 blink rate)
//  - passes    beam breaks + transponder sightings → passings
//  - timing    passings → cars → laps, single/multi-car mode, races
//  - laps      per-car laps, per-driver stats, corrections (undo, restart, delete, join)
//  - cars      transponder cars (learned, saved in flash) and who drives them
//  - drivers   driver names (saved in flash)
//  - zround    USB serial: debug log or ZRound decoder protocol
//  - wlan      Wi-Fi AP + captive portal, home network in the background, mDNS
//  - ota       firmware upload over Wi-Fi
//  - web       HTTP page + API, WebSocket; the page itself lives in the web/ folder

#include <Arduino.h>
#include "cars.h"
#include "config.h"
#include "drivers.h"
#include "ota.h"
#include "sensor.h"
#include "settings.h"
#include "timing.h"
#include "web.h"
#include "wlan.h"
#include "zround.h"

void setup() {
  settingsLoad();
  zroundBegin();                                      // opens the USB serial port (log or ZRound)
  driversLoad();
  carsLoad();
  wlanBegin();
  otaBegin();
  webBegin();
  uint16_t blink[MAX_CARS];
  sensorBegin(blink, carsKnownBlink(blink, MAX_CARS));
  timingBegin();
}

void loop() {
  wlanLoop();
  otaLoop();
  timingLoop();
  zroundLoop();
  webLoop();
  delay(1);
}
