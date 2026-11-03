#include "ota.h"
#include <Arduino.h>
#include <ArduinoOTA.h>
#include "config.h"

void otaBegin() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);              // RC_Lap_Timer is an open network
  ArduinoOTA.setMdnsEnabled(false);                  // wlan.cpp runs mDNS (laptimer.local) itself
  ArduinoOTA.onStart([]() { Serial.println("OTA: receiving firmware"); });
  ArduinoOTA.onEnd([]() { Serial.println("OTA: done, restarting"); });
  ArduinoOTA.onError([](ota_error_t e) { Serial.printf("OTA: error %u\n", e); });
  ArduinoOTA.begin();
}

void otaLoop() { ArduinoOTA.handle(); }
