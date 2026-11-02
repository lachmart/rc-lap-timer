#include "wlan.h"
#include <WiFi.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include "config.h"
#include "settings.h"

static DNSServer dns;
static String    staIp;

static void connectHome() {
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(settings.ssid.c_str(), settings.pass.c_str());
  Serial.printf("Connecting to '%s'", settings.ssid.c_str());

  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < STA_TIMEOUT) { delay(250); Serial.print('.'); }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    staIp = WiFi.localIP().toString();
    WiFi.setAutoReconnect(true);
    Serial.printf("Home Wi-Fi OK, IP %s\n", staIp.c_str());
  } else {
    // A failing network scan hops channels and disturbs the AP, so turn STA off.
    Serial.println("Home Wi-Fi unavailable, running own network only");
    WiFi.disconnect(true);
    WiFi.mode(WIFI_AP);
  }
}

void wlanBegin() {
  WiFi.persistent(false);
  WiFi.setHostname(HOSTNAME);

  if (settings.ssid.length()) connectHome();
  else                        WiFi.mode(WIFI_AP);

  if (strlen(AP_PASS) >= 8) WiFi.softAP(AP_SSID, AP_PASS);
  else                      WiFi.softAP(AP_SSID);
  WiFi.setSleep(false);                               // lower latency, the ESP32 runs on a power bank anyway

  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());                // every domain resolves to us = captive portal

  if (MDNS.begin(HOSTNAME)) MDNS.addService("http", "tcp", 80);

  Serial.printf("Wi-Fi '%s', %s", AP_SSID, wlanApUrl().c_str());
  if (staIp.length()) Serial.printf(" or http://%s.local (%s)", HOSTNAME, staIp.c_str());
  Serial.println();
}

void wlanLoop() { dns.processNextRequest(); }

String wlanStaIp() { return staIp; }

bool wlanIsOwnHost(const String& host) {
  return host == WiFi.softAPIP().toString()
      || (staIp.length() && host == staIp)
      || host.startsWith(HOSTNAME);
}

String wlanApUrl() { return "http://" + WiFi.softAPIP().toString() + "/"; }
