// Wi-Fi: own AP + captive portal DNS (always), optionally the home network (STA), mDNS.
// The home network is joined in the background and retried when lost, see wlan.cpp.
#pragma once
#include <Arduino.h>

struct WlanNetwork {
  String ssid;
  int    rssi;       // dBm
  bool   open;       // no password
};

constexpr int WLAN_MAX_NETWORKS = 15;

void        wlanBegin();                       // reads settings.ssid / settings.pass
void        wlanLoop();
void        wlanApply();                       // home network settings changed: reconnect, the own AP keeps running

String      wlanStaIp();                       // empty when the home network isn't connected
const char* wlanStaState();                    // "off", "connecting", "connected", "waiting"
const char* wlanStaError();                    // why the last attempt failed: "", "wrong password", "network not found", "failed"
int         wlanRssi();                        // dBm of the home network, 0 when not connected

bool        wlanScanStart();                   // false while busy (connecting or already scanning)
bool        wlanScanning();
int         wlanScanResults(const WlanNetwork*& list);   // strongest first, one entry per name

bool        wlanIsOwnHost(const String& host); // the request targets the timer, not a foreign domain
String      wlanApUrl();
