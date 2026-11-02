// RC Lap Timer – ESP32 + laser beam gate (single car)
//
// A laser shines across the track into a photoresistor (LDR). A passing car breaks the beam,
// the ESP32 records the time and sends it to the phone over WebSocket.
//
// Modules
//  - settings  settings in flash (Preferences)
//  - sensor    reads the LDR in a FreeRTOS task, detects crossings
//  - laps      lap bookkeeping
//  - wlan      Wi-Fi AP + captive portal, optional home network, mDNS
//  - web       HTTP page + API, WebSocket; the page itself lives in the web/ folder

#include <Arduino.h>
#include "wlan.h"
#include "sensor.h"
#include "settings.h"
#include "web.h"

void setup() {
  Serial.begin(115200);
  settingsLoad();
  wlanBegin();
  webBegin();
  sensorBegin();
}

void loop() {
  wlanLoop();
  webLoop();
  delay(1);
}
