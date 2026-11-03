#include "ota.h"
#include <Arduino.h>
#include <ArduinoOTA.h>
#include "config.h"
#include "log.h"

void otaBegin() {
  ArduinoOTA.setHostname(HOSTNAME);
  ArduinoOTA.setPassword(OTA_PASSWORD);              // RC_Lap_Timer is an open network
  ArduinoOTA.setMdnsEnabled(false);                  // wlan.cpp runs mDNS (laptimer.local) itself
  ArduinoOTA.onStart([]() { LOGF("OTA: receiving firmware\n"); });
  ArduinoOTA.onEnd([]() { LOGF("OTA: done, restarting\n"); });
  ArduinoOTA.onError([](ota_error_t e) { LOGF("OTA: error %u\n", e); });
  ArduinoOTA.begin();
}

void otaLoop() { ArduinoOTA.handle(); }
