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
#include "webjson.h"
#include "wlan.h"
#include "zround.h"

// Files from web/, embedded at build time (board_build.embed_txtfiles in platformio.ini)
extern const char INDEX_HTML[] asm("_binary_web_index_html_start");
extern const char STYLE_CSS[]  asm("_binary_web_style_css_start");
extern const char APP_JS[]     asm("_binary_web_app_js_start");
extern const char RACE_JS[]    asm("_binary_web_race_js_start");
extern const char SOUND_JS[]   asm("_binary_web_sound_js_start");
extern const char WIFI_JS[]    asm("_binary_web_wifi_js_start");
extern const char RACEVIEW_JS[] asm("_binary_web_raceview_js_start");

// WebServer serves one connection at a time and waits up to 5 s (HTTP_MAX_DATA_WAIT, not overridable)
// for its request. Phone browsers open spare connections that never send one, so every request
// behind them waited 5 s per spare. An idle connection is dropped after IDLE_MS when another waits.
class LapWebServer : public WebServer {
 public:
  using WebServer::WebServer;
  void handleClient() override {
    static constexpr uint32_t IDLE_MS = 200;
    if (_currentStatus == HC_WAIT_READ && !_currentClient.available() &&
        millis() - _statusChange > IDLE_MS && _server.hasClient()) {
      _currentClient.stop();
      _currentClient = NetworkClient();
      _currentStatus = HC_NONE;
    }
    WebServer::handleClient();
  }
};

static LapWebServer     server(80);
static WebSocketsServer ws(81);

static bool stateDirty = false;        // something outside laps changed, send state to all clients

// ---------------------------------------------------------------- JSON (text is written by webjson.cpp)
static std::string buildState() {
  static LapSnapshot s;                // large array, keep it off the stack
  int64_t now = esp_timer_get_time();
  lapsSnapshot(s, now);
  String staIp = wlanStaIp();

  JsonDriver drivers[MAX_DRIVERS];
  int nDrivers = 0;
  for (int i = 0; i < driversCount() && nDrivers < MAX_DRIVERS; i++)
    drivers[nDrivers++] = { driversAt(i).id, driversAt(i).name.c_str() };
  JsonCar cars[MAX_CARS];              // used slots only
  int nCars = 0;
  for (int c = 0; c < MAX_CARS; c++)
    if (carsUsed(c)) cars[nCars++] = { c, carsAt(c).tx, carsAt(c).name.c_str() };

  StateInput in = {};
  in.nowUs           = now;
  in.multi           = settings.multi;
  in.race            = timingRace();
  in.setLaps         = settings.raceLaps;
  in.setMin          = settings.raceMin;
  in.unidentified    = sensorLive().unidentified;
  in.minLapMs        = settings.minLapMs;
  in.delta           = settings.delta;
  in.zroundOn        = settings.zround;
  in.zroundConnected = zroundConnected();
  in.zroundRacing    = zroundRacing();
  in.ssid            = settings.ssid.c_str();
  in.staIp           = staIp.c_str();
  in.sta             = wlanStaState();
  in.staErr          = wlanStaError();
  in.hasPass         = settings.pass.length() > 0;
  in.drivers         = drivers;
  in.nDrivers        = nDrivers;
  in.cars            = cars;
  in.nCars           = nCars;
  in.laps            = &s;

  std::string j;
  webjsonState(j, in);
  return j;
}

static std::string buildLive() {
  int64_t now = esp_timer_get_time();
  LiveInput in;
  in.sensor = sensorLive();
  for (int c = 0; c < MAX_CARS; c++) in.cur[c] = lapsCurrentMs(c, now);
  in.rssi = wlanRssi();
  std::string j;
  webjsonLive(j, in);
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

// order = comma-separated driver ids for heats (one car, laps race), e.g. order=3,1,2
static void handleRaceStart() {
  uint8_t order[MAX_DRIVERS];
  int n = 0;
  String list = server.arg("order");
  for (int pos = 0; pos < (int)list.length() && n < MAX_DRIVERS;) {
    int comma = list.indexOf(',', pos);
    if (comma < 0) comma = list.length();
    long id = list.substring(pos, comma).toInt();
    if (id > 0 && id < 256) order[n++] = (uint8_t)id;
    pos = comma + 1;
  }
  timingRaceStart(argInt("laps"), argInt("minutes"), order, n);
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
  const WlanNetwork* list;
  int n = wlanScanResults(list);
  JsonNetwork nets[WLAN_MAX_NETWORKS];
  for (int i = 0; i < n && i < WLAN_MAX_NETWORKS; i++) nets[i] = { list[i].ssid.c_str(), list[i].rssi, list[i].open };
  std::string j;
  webjsonScan(j, wlanScanning(), nets, n < WLAN_MAX_NETWORKS ? n : WLAN_MAX_NETWORKS);
  server.send(200, "application/json", j.c_str());
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
    std::string s = buildState();
    ws.sendTXT(num, s.c_str(), s.size());
  }
}

// ---------------------------------------------------------------- public
void webBegin() {
  server.on("/",          HTTP_GET, []() { serveStatic("text/html", INDEX_HTML); });
  server.on("/style.css", HTTP_GET, []() { serveStatic("text/css", STYLE_CSS); });
  server.on("/app.js",    HTTP_GET, []() { serveStatic("application/javascript", APP_JS); });
  server.on("/race.js",   HTTP_GET, []() { serveStatic("application/javascript", RACE_JS); });
  server.on("/sound.js",  HTTP_GET, []() { serveStatic("application/javascript", SOUND_JS); });
  server.on("/wifi.js",   HTTP_GET, []() { serveStatic("application/javascript", WIFI_JS); });
  server.on("/raceview.js", HTTP_GET, []() { serveStatic("application/javascript", RACEVIEW_JS); });
  server.on("/api/state", HTTP_GET, []() { server.send(200, "application/json", buildState().c_str()); });

  // session
  server.on("/api/reset",      HTTP_POST, []() { timingNewSession(); sendResult(true); });
  server.on("/api/undo",       HTTP_POST, []() { sendResult(timingUndo(argInt("car"))); });
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
  server.on("/api/race/start",  HTTP_POST, handleRaceStart);
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
    std::string s = buildState();
    ws.broadcastTXT(s.c_str(), s.size());
  }

  if (millis() - lastLive >= LIVE_EVERY && ws.connectedClients() > 0) {
    lastLive = millis();
    std::string l = buildLive();
    ws.broadcastTXT(l.c_str(), l.size());
  }
}
