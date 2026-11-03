// RC Lap Timer – ESP32 + laser beam gate (single car, drivers take turns)
//
// A laser shines across the track into a photoresistor (LDR). A passing car breaks the beam,
// the ESP32 records the time and sends it to the phone over WebSocket.
//
// Modules
//  - settings  settings in flash (Preferences)
//  - sensor    reads the LDR in a FreeRTOS task, detects crossings, beam LED
//  - laps      laps, per-driver stats, corrections (undo, restart, delete)
//  - drivers   driver names (saved in flash), who's driving now
//  - wlan      Wi-Fi AP + captive portal, home network in the background, mDNS
//  - web       HTTP page + API, WebSocket; the page itself lives in the web/ folder

#include <Arduino.h>
#include "drivers.h"
#include "sensor.h"
#include "settings.h"
#include "web.h"
#include "wlan.h"

void setup() {
  Serial.begin(115200);
  settingsLoad();
  driversLoad();
  wlanBegin();
  webBegin();
  sensorBegin();
}

void loop() {
  wlanLoop();
  webLoop();
  delay(1);
}
