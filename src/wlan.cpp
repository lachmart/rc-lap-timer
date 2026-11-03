#include "wlan.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include "config.h"
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
enum class Sta : uint8_t { Off, Connecting, Connected, Waiting };

static DNSServer dns;
static Sta       sta     = Sta::Off;
static uint32_t  since   = 0;          // when the current state started
static String    staIp;

static void startMdns() {
  MDNS.end();
  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);
}

static void startConnecting() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(settings.ssid.c_str(), settings.pass.c_str());
  sta = Sta::Connecting;
  since = millis();
  Serial.printf("Connecting to home Wi-Fi '%s'\n", settings.ssid.c_str());
}

static void stopConnecting(const char* why) {
  WiFi.disconnect(true);
  WiFi.mode(WIFI_AP);
  sta = Sta::Waiting;
  since = millis();
  Serial.printf("Home Wi-Fi not connected (%s), retrying later\n", why);
}

void wlanBegin() {
  WiFi.persistent(false);
  WiFi.setHostname(HOSTNAME);
  WiFi.mode(settings.ssid.length() ? WIFI_AP_STA : WIFI_AP);

  if (strlen(AP_PASS) >= 8) WiFi.softAP(AP_SSID, AP_PASS);
  else                      WiFi.softAP(AP_SSID);
  WiFi.setSleep(false);                               // lower latency, the ESP32 runs on a power bank anyway
  WiFi.setAutoReconnect(true);

  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());                // every domain resolves to us = captive portal
  startMdns();

  Serial.printf("Own Wi-Fi '%s', %s\n", AP_SSID, wlanApUrl().c_str());
  if (settings.ssid.length()) startConnecting();
}

void wlanLoop() {
  dns.processNextRequest();

  bool up = WiFi.status() == WL_CONNECTED;
  uint32_t elapsed = millis() - since;

  switch (sta) {
    case Sta::Off:
      break;

    case Sta::Connecting:
      if (up) {
        sta = Sta::Connected;
        staIp = WiFi.localIP().toString();
        startMdns();                                  // announce laptimer.local on the new network too
        Serial.printf("Home Wi-Fi OK, http://%s.local or http://%s\n", HOSTNAME, staIp.c_str());
      } else if (WiFi.softAPgetStationNum() > 0) {
        stopConnecting("a phone is on the own network");
      } else if (elapsed >= STA_TIMEOUT) {
        stopConnecting("not in range");
      }
      break;

    case Sta::Connected:
      if (!up) {                                      // auto-reconnect is on, give it time
        staIp = "";
        sta = Sta::Connecting;
        since = millis();
        Serial.println("Home Wi-Fi lost, reconnecting");
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

bool wlanIsOwnHost(const String& host) {
  return host == WiFi.softAPIP().toString()
      || (staIp.length() && host == staIp)
      || host.startsWith(HOSTNAME);
}

String wlanApUrl() { return "http://" + WiFi.softAPIP().toString() + "/"; }
