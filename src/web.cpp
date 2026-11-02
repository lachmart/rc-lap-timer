#include "web.h"
#include <Arduino.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include "config.h"
#include "laps.h"
#include "wlan.h"
#include "sensor.h"
#include "settings.h"

// Files from web/, embedded at build time (board_build.embed_txtfiles in platformio.ini)
extern const char INDEX_HTML[] asm("_binary_web_index_html_start");
extern const char STYLE_CSS[]  asm("_binary_web_style_css_start");
extern const char APP_JS[]     asm("_binary_web_app_js_start");

static WebServer        server(80);
static WebSocketsServer ws(81);

static bool     configDirty = false;   // settings changed, send state to all clients
static uint32_t restartAt   = 0;

// ---------------------------------------------------------------- JSON
static String buildState() {
  static LapSnapshot s;                // large array, keep it off the stack
  lapsSnapshot(s);

  String j;
  j.reserve(320 + s.n * 8);
  j += "{\"t\":\"state\",\"running\":"; j += s.running ? "true" : "false";
  j += ",\"cur\":";     j += s.curMs;
  j += ",\"count\":";   j += s.count;
  j += ",\"best\":";    j += s.bestMs;
  j += ",\"ignored\":"; j += s.ignored;
  j += ",\"minLap\":";  j += settings.minLapMs;
  j += ",\"delta\":";   j += settings.delta;
  j += ",\"ssid\":\"";  j += settings.ssid; j += '"';   // SSID has no quotes, see settingsSaveWifi
  j += ",\"staIp\":\""; j += wlanStaIp(); j += '"';
  j += ",\"laps\":[";
  for (int i = 0; i < s.n; i++) { if (i) j += ','; j += s.laps[i]; }
  j += "]}";
  return j;
}

static String buildLive() {
  SensorLive l = sensorLive();
  String j = "{\"t\":\"live\",\"raw\":";
  j += l.raw;
  j += ",\"base\":";   j += l.base;
  j += ",\"broken\":"; j += l.broken ? "true" : "false";
  j += ",\"cur\":";    j += lapsCurrentMs();
  j += '}';
  return j;
}

// ---------------------------------------------------------------- HTTP
static void sendOk() { server.send(200, "application/json", "{\"ok\":true}"); }

static void serveStatic(const char* type, const char* body) {
  server.sendHeader("Cache-Control", "no-cache");
  server.send_P(200, type, body);
}

static void handleConfig() {
  long minLap = server.hasArg("minLap") ? server.arg("minLap").toInt() : settings.minLapMs;
  long delta  = server.hasArg("delta")  ? server.arg("delta").toInt()  : settings.delta;
  settingsSaveTiming(minLap, delta);
  configDirty = true;
  sendOk();
}

static void handleWifi() {
  settingsSaveWifi(server.arg("ssid"), server.arg("pass"));
  sendOk();
  restartAt = millis() + 800;                         // let the response go out, then restart
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
  server.on("/",              HTTP_GET,  []() { serveStatic("text/html", INDEX_HTML); });
  server.on("/style.css",     HTTP_GET,  []() { serveStatic("text/css", STYLE_CSS); });
  server.on("/app.js",        HTTP_GET,  []() { serveStatic("application/javascript", APP_JS); });
  server.on("/api/state",     HTTP_GET,  []() { server.send(200, "application/json", buildState()); });
  server.on("/api/reset",     HTTP_POST, []() { lapsReset(); sendOk(); });
  server.on("/api/calibrate", HTTP_POST, []() { sensorCalibrate(); sendOk(); });
  server.on("/api/config",    HTTP_POST, handleConfig);
  server.on("/api/wifi",      HTTP_POST, handleWifi);
  server.onNotFound(handleNotFound);
  server.begin();

  ws.begin();
  ws.onEvent(onWsEvent);
}

void webLoop() {
  static uint32_t sentRevision = 0;
  static uint32_t lastLive     = 0;

  server.handleClient();
  ws.loop();

  uint32_t rev = lapsRevision();
  if (rev != sentRevision || configDirty) {           // a new lap goes out immediately
    sentRevision = rev;
    configDirty  = false;
    String s = buildState();
    ws.broadcastTXT(s);
  }

  if (millis() - lastLive >= LIVE_EVERY && ws.connectedClients() > 0) {
    lastLive = millis();
    String l = buildLive();
    ws.broadcastTXT(l);
  }

  if (restartAt && (int32_t)(millis() - restartAt) >= 0) ESP.restart();
}
