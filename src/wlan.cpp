#include "wlan.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include "config.h"
#include "log.h"
#include "settings.h"

// Home network (STA) state machine, runs from wlanLoop() without blocking:
//
//   Connecting ──connected──▶ Connected ──lost──▶ Connecting
//       │ timeout, or a phone joined the own AP
//       ▼
//    Waiting ──STA_RETRY passed and nobody on the own AP──▶ Connecting
//
// While connecting, the ESP32 hops channels to search for the router, which disturbs phones
// on the own AP. That's why it only searches while nobody uses the own AP.
// A network scan (user asked for it in the UI) hops channels too, but only for ~2 s.
enum class Sta : uint8_t { Off, Connecting, Connected, Waiting };

static DNSServer dns;
static Sta       sta     = Sta::Off;
static uint32_t  since   = 0;          // when the current state started
static String    staIp;

static volatile uint8_t lastReason = 0;   // disconnect reason from the Wi-Fi event task

static bool        scanning = false;
static WlanNetwork networks[WLAN_MAX_NETWORKS];
static int         networkCount = 0;

static void startMdns() {
  MDNS.end();
  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
}

static void onDisconnected(arduino_event_id_t, arduino_event_info_t info) {
  uint8_t reason = info.wifi_sta_disconnected.reason;
  if (reason != WIFI_REASON_ASSOC_LEAVE) lastReason = reason;   // our own disconnect() isn't a failure
}

static void startConnecting() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.disconnect();                                  // drop a previous network, if any
  WiFi.begin(settings.ssid.c_str(), settings.pass.c_str());
  sta = Sta::Connecting;
  since = millis();
  LOGF("Connecting to home Wi-Fi '%s'\n", settings.ssid.c_str());
}

static void stopConnecting(const char* why) {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  sta = Sta::Waiting;
  since = millis();
  LOGF("Home Wi-Fi not connected (%s, %s), retrying later\n", why, wlanStaError());
}

void wlanBegin() {
  WiFi.persistent(false);
  WiFi.setHostname(HOSTNAME);
  WiFi.onEvent(onDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);
  WiFi.mode(settings.ssid.length() ? WIFI_AP_STA : WIFI_AP);

  if (strlen(AP_PASS) >= 8) WiFi.softAP(AP_SSID, AP_PASS);
  else                      WiFi.softAP(AP_SSID);
  WiFi.setSleep(false);                               // lower latency, the ESP32 runs on a power bank anyway
  WiFi.setAutoReconnect(true);

  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());                // every domain resolves to us = captive portal
  startMdns();

  LOGF("Own Wi-Fi '%s', %s\n", AP_SSID, wlanApUrl().c_str());
  if (settings.ssid.length()) startConnecting();
}

void wlanApply() {
  staIp = "";
  lastReason = 0;
  if (settings.ssid.length()) {
    startConnecting();
  } else {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
    sta = Sta::Off;
    startMdns();
    LOGF("Home Wi-Fi forgotten, own network only\n");
  }
}

// Copy finished scan results: one entry per network name (strongest), strongest first.
static void collectScan(int found) {
  networkCount = 0;
  for (int i = 0; i < found; i++) {
    String ssid = WiFi.SSID(i);
    if (!ssid.length() || ssid == AP_SSID) continue;
    int rssi = WiFi.RSSI(i);
    bool open = WiFi.encryptionType(i) == WIFI_AUTH_OPEN;

    int at = -1;
    for (int k = 0; k < networkCount; k++) if (networks[k].ssid == ssid) at = k;
    if (at >= 0) {
      if (rssi > networks[at].rssi) networks[at].rssi = rssi;
      continue;
    }
    if (networkCount < WLAN_MAX_NETWORKS) networks[networkCount++] = { ssid, rssi, open };
  }
  for (int i = 1; i < networkCount; i++)              // insertion sort by signal
    for (int k = i; k > 0 && networks[k].rssi > networks[k - 1].rssi; k--) std::swap(networks[k], networks[k - 1]);
}

bool wlanScanStart() {
  if (scanning || sta == Sta::Connecting) return false;
  WiFi.mode(WIFI_AP_STA);                             // scanning needs the station interface
  if (WiFi.scanNetworks(true, false, false, 120) != WIFI_SCAN_RUNNING) {
    if (sta != Sta::Connected) WiFi.mode(WIFI_AP);
    return false;
  }
  scanning = true;
  return true;
}

bool wlanScanning() { return scanning; }

int wlanScanResults(const WlanNetwork*& list) {
  list = networks;
  return networkCount;
}

static void scanLoop() {
  if (!scanning) return;
  int found = WiFi.scanComplete();
  if (found == WIFI_SCAN_RUNNING) return;
  if (found >= 0) collectScan(found);
  WiFi.scanDelete();
  scanning = false;
  if (sta == Sta::Off || sta == Sta::Waiting) WiFi.mode(WIFI_AP);
}

void wlanLoop() {
  dns.processNextRequest();
  scanLoop();
  if (scanning) return;                               // don't start connecting in the middle of a scan

  bool up = WiFi.status() == WL_CONNECTED;
  uint32_t elapsed = millis() - since;

  switch (sta) {
    case Sta::Off:
      break;

    case Sta::Connecting:
      if (up) {
        sta = Sta::Connected;
        staIp = WiFi.localIP().toString();
        lastReason = 0;
        startMdns();                                  // announce laptimer.local on the new network too
        LOGF("Home Wi-Fi OK, http://%s.local or http://%s\n", HOSTNAME, staIp.c_str());
      } else if (WiFi.softAPgetStationNum() > 0 && elapsed > 2000) {
        stopConnecting("a phone is on the own network");
      } else if (elapsed >= STA_TIMEOUT) {
        stopConnecting("timeout");
      }
      break;

    case Sta::Connected:
      if (!up) {                                      // auto-reconnect is on, give it time
        staIp = "";
        sta = Sta::Connecting;
        since = millis();
        LOGF("Home Wi-Fi lost, reconnecting\n");
      }
      break;

    case Sta::Waiting:
      if (elapsed >= STA_RETRY && WiFi.softAPgetStationNum() == 0) startConnecting();
      break;
  }
}

String wlanStaIp() { return staIp; }

const char* wlanStaState() {
  switch (sta) {
    case Sta::Connecting: return "connecting";
    case Sta::Connected:  return "connected";
    case Sta::Waiting:    return "waiting";
    default:              return "off";
  }
}

const char* wlanStaError() {
  if (sta != Sta::Waiting) return "";                 // only after an attempt has failed
  switch (lastReason) {
    case 0:                                         return "";
    case WIFI_REASON_AUTH_FAIL:
    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
    case WIFI_REASON_HANDSHAKE_TIMEOUT:             return "wrong password";
    case WIFI_REASON_NO_AP_FOUND:
    case WIFI_REASON_NO_AP_FOUND_W_COMPATIBLE_SECURITY:
    case WIFI_REASON_NO_AP_FOUND_IN_AUTHMODE_THRESHOLD:
    case WIFI_REASON_NO_AP_FOUND_IN_RSSI_THRESHOLD: return "network not found";
    default:                                        return "failed";
  }
}

int wlanRssi() { return sta == Sta::Connected ? WiFi.RSSI() : 0; }

bool wlanIsOwnHost(const String& host) {
  return host == WiFi.softAPIP().toString()
      || (staIp.length() && host == staIp)
      || host.startsWith(HOSTNAME);
}

String wlanApUrl() { return "http://" + WiFi.softAPIP().toString() + "/"; }
