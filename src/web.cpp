#include "web.h"
#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "esp_timer.h"
#include "config.h"
#include "drivers.h"
#include "laps.h"
#include "sensor.h"
#include "settings.h"
#include "wlan.h"

// Files from web/, embedded at build time (board_build.embed_txtfiles in platformio.ini)
extern const char INDEX_HTML[] asm("_binary_web_index_html_start");
extern const char STYLE_CSS[]  asm("_binary_web_style_css_start");
extern const char APP_JS[]     asm("_binary_web_app_js_start");

static WebServer        server(80);
static WebSocketsServer ws(81);

static bool stateDirty = false;        // something outside laps changed, send state to all clients

// ---------------------------------------------------------------- JSON
static void addJsonString(String& j, const String& s) {
  j += '"';
  for (unsigned i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { j += '\\'; j += c; }
    else if ((uint8_t)c < 0x20) { char esc[7]; snprintf(esc, sizeof esc, "\\u%04x", c); j += esc; }
    else j += c;
  }
  j += '"';
}

static String buildState() {
  static LapSnapshot s;                // large array, keep it off the stack
  lapsSnapshot(s, esp_timer_get_time());

  String j;
  j.reserve(600 + s.n * 16);
  j += "{\"t\":\"state\",\"running\":"; j += s.running ? "true" : "false";
  j += ",\"cur\":";     j += s.curMs;
  j += ",\"driver\":";  j += s.driver;
  j += ",\"canUndo\":"; j += s.canUndo ? "true" : "false";
  j += ",\"count\":";   j += s.count;
  j += ",\"best\":";    j += s.bestMs;
  j += ",\"ignored\":"; j += s.ignored;
  j += ",\"minLap\":";  j += settings.minLapMs;
  j += ",\"delta\":";   j += settings.delta;
  j += ",\"ssid\":";    addJsonString(j, settings.ssid);
  j += ",\"staIp\":\""; j += wlanStaIp(); j += '"';
  j += ",\"sta\":\"";   j += wlanStaState(); j += '"';
  j += ",\"staErr\":\""; j += wlanStaError(); j += '"';
  j += ",\"hasPass\":";  j += settings.pass.length() ? "true" : "false";

  j += ",\"drivers\":[";
  for (int i = 0; i < driversCount(); i++) {
    if (i) j += ',';
    j += "{\"id\":"; j += driversAt(i).id;
    j += ",\"name\":"; addJsonString(j, driversAt(i).name);
    j += '}';
  }
  j += "],\"stats\":[";
  for (int i = 0; i < s.nStats; i++) {
    const DriverStats& d = s.stats[i];
    if (i) j += ',';
    j += "{\"id\":";     j += d.id;
    j += ",\"count\":";  j += d.count;
    j += ",\"best\":";   j += d.bestMs;
    j += ",\"total\":";  j += d.totalMs;
    j += '}';
  }
  j += "],\"laps\":[";                 // [ms, driver, deleted, continues previous lap], oldest first
  for (int i = 0; i < s.n; i++) {
    const Lap& l = s.laps[i];
    bool cont = i > 0 && lapsContinues(s.laps[i - 1], l);
    if (i) j += ',';
    j += '['; j += l.ms; j += ','; j += l.driver;
    j += ','; j += l.deleted ? '1' : '0';
    j += ','; j += cont ? '1' : '0';
    j += ']';
  }
  j += "]}";
  return j;
}

static String buildLive() {
  SensorLive l = sensorLive();
  String j = "{\"t\":\"live\",\"raw\":";
  j += l.raw;
  j += ",\"base\":";   j += l.base;
  j += ",\"broken\":"; j += l.broken ? "true" : "false";
  j += ",\"cur\":";    j += lapsCurrentMs(esp_timer_get_time());
  j += ",\"noise\":";  j += l.noise;
  j += ",\"peak\":";   j += l.peak;
  j += ",\"suggest\":"; j += l.suggest;
  j += ",\"rssi\":";   j += wlanRssi();
  j += '}';
  return j;
}

// ---------------------------------------------------------------- HTTP
static void sendResult(bool ok) {
  stateDirty = true;
  if (ok) server.send(200, "application/json", "{\"ok\":true}");
  else    server.send(400, "application/json", "{\"ok\":false}");
}

static long argInt(const char* name) { return server.arg(name).toInt(); }

static void serveStatic(const char* type, const char* body) {
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, type, body);
}

static void handleConfig() {
  long minLap = server.hasArg("minLap") ? argInt("minLap") : (long)settings.minLapMs;
  long delta  = server.hasArg("delta")  ? argInt("delta")  : (long)settings.delta;
  settingsSaveTiming(minLap, delta);
  sendResult(true);
}

// Without a pass argument the saved password stays (same network, e.g. only re-connecting).
static void handleWifi() {
  String ssid = server.arg("ssid");
  ssid.trim();
  String pass = server.hasArg("pass") || ssid != settings.ssid ? server.arg("pass") : settings.pass;
  settingsSaveWifi(ssid, pass);
  wlanApply();                                        // no restart, the own network keeps running
  sendResult(true);
}

static void handleScanResults() {
  String j = "{\"scanning\":";
  j += wlanScanning() ? "true" : "false";
  j += ",\"networks\":[";
  const WlanNetwork* list;
  int n = wlanScanResults(list);
  for (int i = 0; i < n; i++) {
    if (i) j += ',';
    j += "{\"ssid\":"; addJsonString(j, list[i].ssid);
    j += ",\"rssi\":"; j += list[i].rssi;
    j += ",\"open\":"; j += list[i].open ? "true" : "false";
    j += '}';
  }
  j += "]}";
  server.send(200, "application/json", j);
}

// Captive portal: phones probe URLs like /generate_204 or /hotspot-detect.html.
// Anything unknown gets redirected to the timer page, and the phone then opens it by itself.
static void handleNotFound() {
  if (!wlanIsOwnHost(server.hostHeader())) {
    server.sendHeader("Location", wlanApUrl(), true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

// ---------------------------------------------------------------- WebSocket
static void onWsEvent(uint8_t num, WStype_t type, uint8_t*, size_t) {
  if (type == WStype_CONNECTED) {
    String s = buildState();
    ws.sendTXT(num, s);
  }
}

// ---------------------------------------------------------------- public
void webBegin() {
  server.on("/",          HTTP_GET, []() { serveStatic("text/html", INDEX_HTML); });
  server.on("/style.css", HTTP_GET, []() { serveStatic("text/css", STYLE_CSS); });
  server.on("/app.js",    HTTP_GET, []() { serveStatic("application/javascript", APP_JS); });
  server.on("/api/state", HTTP_GET, []() { server.send(200, "application/json", buildState()); });

  // session
  server.on("/api/reset",      HTTP_POST, []() { lapsReset(); sendResult(true); });
  server.on("/api/undo",       HTTP_POST, []() { sendResult(lapsUndo()); });
  server.on("/api/restart",    HTTP_POST, []() { lapsRestart(); sendResult(true); });
  server.on("/api/lap/delete", HTTP_POST, []() { sendResult(lapsDelete(argInt("n"))); });
  server.on("/api/lap/join",   HTTP_POST, []() { sendResult(lapsJoin(argInt("n"))); });

  // drivers
  server.on("/api/driver/add",    HTTP_POST, []() { sendResult(driversAdd(server.arg("name")) != 0); });
  server.on("/api/driver/remove", HTTP_POST, []() { sendResult(driversRemove(argInt("id"))); });
  server.on("/api/driver/select", HTTP_POST, []() { sendResult(driversSelect(argInt("id"))); });

  // settings
  server.on("/api/calibrate", HTTP_POST, []() { sensorCalibrate(); sendResult(true); });
  server.on("/api/config",    HTTP_POST, handleConfig);
  server.on("/api/wifi",      HTTP_POST, handleWifi);
  server.on("/api/wifi/scan", HTTP_POST, []() { sendResult(wlanScanStart()); });
  server.on("/api/wifi/scan", HTTP_GET,  handleScanResults);

  server.onNotFound(handleNotFound);
  server.begin();

  ws.begin();
  ws.onEvent(onWsEvent);
}

void webLoop() {
  static uint32_t    sentRevision = 0;
  static const char* sentSta      = "";
  static const char* sentStaErr   = "";
  static uint32_t    lastLive     = 0;

  server.handleClient();
  ws.loop();

  uint32_t    rev = lapsRevision();
  const char* sta = wlanStaState();
  const char* err = wlanStaError();                   // string literals, comparing pointers is enough
  if (rev != sentRevision || sta != sentSta || err != sentStaErr || stateDirty) {   // a new lap goes out immediately
    sentRevision = rev;
    sentSta      = sta;
    sentStaErr   = err;
    stateDirty   = false;
    String s = buildState();
    ws.broadcastTXT(s);
  }

  if (millis() - lastLive >= LIVE_EVERY && ws.connectedClients() > 0) {
    lastLive = millis();
    String l = buildLive();
    ws.broadcastTXT(l);
  }
}
