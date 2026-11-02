// Wi-Fi: own AP + captive portal DNS, optionally the home network (STA), mDNS
#pragma once
#include <Arduino.h>

void   wlanBegin();                       // reads settings.ssid / settings.pass
void   wlanLoop();
String wlanStaIp();                       // empty when the home network isn't connected
bool   wlanIsOwnHost(const String& host); // the request targets the timer, not a foreign domain
String wlanApUrl();
