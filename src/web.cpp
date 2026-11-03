#include "web.h"
#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "esp_timer.h"
#include "config.h"
#include "cars.h"
#include "drivers.h"
#include "laps.h"
#include "sensor.h"
#include "settings.h"
#include "timing.h"
#include "wlan.h"
#include "zround.h"

// Files from web/, embedded at build time (board_build.embed_txtfiles in platformio.ini)
extern const char INDEX_HTML[] asm("_binary_web_index_html_start");
extern const char STYLE_CSS[]  asm("_binary_web_style_css_start");
extern const char APP_JS[]     asm("_binary_web_app_js_start");
extern const char RACE_JS[]    asm("_binary_web_race_js_start");

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

static const char* raceStateName(RaceState r) {
  switch (r) {
    case RaceState::Countdown: return "countdown";
    case RaceState::Running:   return "running";
    default:                   return "off";
  }
}

static const char* jsonBool(bool b) { return b ? "true" : "false"; }

static void addRace(String& j) {
  RaceInfo race = timingRace();
  j += ",\"race\":{\"state\":\""; j += raceStateName(race.state);
  j += "\",\"go\":";    j += (long)(race.goUs / 1000);
  j += ",\"laps\":";    j += race.laps;
  j += ",\"minutes\":"; j += race.minutes;
  j += ",\"setLaps\":"; j += settings.raceLaps;
  j += ",\"setMin\":";  j += settings.raceMin;
  j += '}';
}

static void addDrivers(String& j) {
  j += ",\"drivers\":[";
  for (int i = 0; i < driversCount(); i++) {
    if (i) j += ',';
    j += "{\"id\":"; j += driversAt(i).id;
    j += ",\"name\":"; addJsonString(j, driversAt(i).name);
    j += '}';
  }
  j += ']';
}

static void addCars(String& j, const LapSnapshot& s) {
  j += ",\"cars\":[";                // used slots only
  bool first = true;
  for (int c = 0; c < MAX_CARS; c++) {
    if (!carsUsed(c)) continue;
    const CarState& cs = s.cars[c];
    if (!first) j += ',';
    first = false;
    j += "{\"slot\":";    j += c;
    j += ",\"tx\":";      j += carsAt(c).tx;
    j += ",\"name\":";    addJsonString(j, carsAt(c).name);
    j += ",\"driver\":";  j += cs.driver;
    j += ",\"running\":"; j += jsonBool(cs.running);
    j += ",\"cur\":";     j += cs.curMs;
    j += ",\"canUndo\":"; j += jsonBool(cs.canUndo);
    j += ",\"laps\":";    j += cs.laps;
    j += '}';
  }
  j += ']';
}

static void addStats(String& j, const LapSnapshot& s) {
  j += ",\"stats\":[";
  for (int i = 0; i < s.nStats; i++) {
    const DriverStats& d = s.stats[i];
    if (i) j += ',';
    j += "{\"key\":";    j += d.key;
    j += ",\"count\":";  j += d.count;
    j += ",\"best\":";   j += d.bestMs;
    j += ",\"total\":";  j += d.totalMs;
    j += '}';
  }
  j += ']';
}

// [ms, driver, deleted, continues the car's previous lap, car, lap number of the car, precise, end ms], oldest first
static void addLaps(String& j, const LapSnapshot& s) {
  j += ",\"laps\":[";
  int prevOfCar[MAX_CARS];
  for (int c = 0; c < MAX_CARS; c++) prevOfCar[c] = -1;
  for (int i = 0; i < s.n; i++) {
    const Lap& l = s.laps[i];
    bool cont = prevOfCar[l.car] >= 0 && lapsContinues(s.laps[prevOfCar[l.car]], l);
    prevOfCar[l.car] = i;
    if (i) j += ',';
    j += '['; j += l.ms; j += ','; j += l.driver;
    j += ','; j += l.deleted ? '1' : '0';
    j += ','; j += cont ? '1' : '0';
    j += ','; j += l.car;
    j += ','; j += l.num;
    j += ','; j += l.precise ? '1' : '0';
    j += ','; j += (long)(l.endUs / 1000);
    j += ']';
  }
  j += ']';
}

static String buildState() {
  static LapSnapshot s;                // large array, keep it off the stack
  int64_t now = esp_timer_get_time();
  lapsSnapshot(s, now);
  SensorLive live = sensorLive();

  String j;
  j.reserve(1200 + s.n * 44);
  j += "{\"t\":\"state\",\"now\":"; j += (long)(now / 1000);   // ms; client converts the other times with it
  j += ",\"multi\":";   j += jsonBool(settings.multi);
  addRace(j);
  j += ",\"count\":";   j += s.count;
  j += ",\"best\":";    j += s.bestMs;
  j += ",\"ignored\":"; j += s.ignored;
  j += ",\"unident\":"; j += live.unidentified;
  j += ",\"minLap\":";  j += settings.minLapMs;
  j += ",\"delta\":";   j += settings.delta;
  j += ",\"zround\":{\"on\":"; j += jsonBool(settings.zround);
  j += ",\"connected\":"; j += jsonBool(zroundConnected());
  j += ",\"racing\":";  j += jsonBool(zroundRacing());
  j += "},\"ssid\":";   addJsonString(j, settings.ssid);
  j += ",\"staIp\":\""; j += wlanStaIp(); j += '"';
  j += ",\"sta\":\"";   j += wlanStaState(); j += '"';
  j += ",\"staErr\":\""; j += wlanStaError(); j += '"';
  j += ",\"hasPass\":";  j += jsonBool(settings.pass.length());
  addDrivers(j);
  addCars(j, s);
  addStats(j, s);
  addLaps(j, s);
  j += '}';
  return j;
}

static String buildLive() {
  SensorLive l = sensorLive();
  int64_t now = esp_timer_get_time();
  String j = "{\"t\":\"live\",\"raw\":";
  j += l.raw;
  j += ",\"base\":";    j += l.base;
  j += ",\"broken\":";  j += jsonBool(l.broken);
  j += ",\"cur\":[";                   // lap in progress per car slot
  for (int c = 0; c < MAX_CARS; c++) { if (c) j += ','; j += lapsCurrentMs(c, now); }
  j += "],\"noise\":";  j += l.noise;
  j += ",\"peak\":";    j += l.peak;
  j += ",\"suggest\":"; j += l.suggest;
  j += ",\"irId\":";    j += l.irId;
  j += ",\"irAgo\":";   j += l.irAgoMs;
  j += ",\"unident\":"; j += l.unidentified;
  j += ",\"rssi\":";    j += wlanRssi();
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
  j += jsonBool(wlanScanning());
  j += ",\"networks\":[";
  const WlanNetwork* list;
  int n = wlanScanResults(list);
  for (int i = 0; i < n; i++) {
    if (i) j += ',';
    j += "{\"ssid\":"; addJsonString(j, list[i].ssid);
    j += ",\"rssi\":"; j += list[i].rssi;
    j += ",\"open\":"; j += jsonBool(list[i].open);
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
  server.on("/race.js",   HTTP_GET, []() { serveStatic("application/javascript", RACE_JS); });
  server.on("/api/state", HTTP_GET, []() { server.send(200, "application/json", buildState()); });

  // session
  server.on("/api/reset",      HTTP_POST, []() { timingNewSession(); sendResult(true); });
  server.on("/api/undo",       HTTP_POST, []() { sendResult(lapsUndo(argInt("car"))); });
  server.on("/api/restart",    HTTP_POST, []() { lapsRestart(argInt("car")); sendResult(true); });
  server.on("/api/lap/delete", HTTP_POST, []() { sendResult(lapsDelete(argInt("n"))); });
  server.on("/api/lap/join",   HTTP_POST, []() { sendResult(lapsJoin(argInt("n"))); });

  // drivers
  server.on("/api/driver/add",    HTTP_POST, []() { sendResult(driversAdd(server.arg("name")) != 0); });
  server.on("/api/driver/remove", HTTP_POST, []() { sendResult(driversRemove(argInt("id"))); });
  server.on("/api/driver/select", HTTP_POST, []() { sendResult(carsSetDriver(argInt("car"), argInt("id"))); });

  // cars and races
  server.on("/api/car/rename",  HTTP_POST, []() { sendResult(carsRename(argInt("slot"), server.arg("name"))); });
  server.on("/api/car/remove",  HTTP_POST, []() { sendResult(carsRemove(argInt("slot"))); });
  server.on("/api/mode",        HTTP_POST, []() { timingSetMulti(argInt("multi") != 0); sendResult(true); });
  server.on("/api/race/start",  HTTP_POST, []() { timingRaceStart(argInt("laps"), argInt("minutes")); sendResult(true); });
  server.on("/api/race/stop",   HTTP_POST, []() { timingRaceStop(); sendResult(true); });
  server.on("/api/usb",         HTTP_POST, []() {
    settingsSaveMode(settings.multi, argInt("zround") != 0);
    sendResult(true);
    zroundApply();
  });

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
