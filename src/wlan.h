// Wi-Fi: own AP + captive portal DNS (always), optionally the home network (STA), mDNS.
// The home network is joined in the background and retried when lost, see wlan.cpp.
#pragma once
#include <Arduino.h>

void        wlanBegin();                       // reads settings.ssid / settings.pass
void        wlanLoop();
String      wlanStaIp();                       // empty when the home network isn't connected
const char* wlanStaState();                    // "off", "connecting", "connected", "waiting"
bool        wlanIsOwnHost(const String& host); // the request targets the timer, not a foreign domain
String      wlanApUrl();
