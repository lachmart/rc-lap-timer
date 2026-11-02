// RC Lap Timer – ESP32 + laserová závora (jedno auto)
//
// Laser svítí přes trať do fotoodporu (LDR). Průjezd auta přeruší paprsek,
// ESP32 zaznamená čas a pošle ho přes WebSocket do mobilu.
//
// Připojení
//  - Wi-Fi "RC_Lap_Timer" vždy běží. Po připojení se stránka sama otevře (captive portal),
//    případně ručně http://192.168.4.1
//  - Volitelně se ESP32 připojí i k domácí Wi-Fi (nastavení na stránce),
//    pak je dostupné na http://laptimer.local nebo přes IP z routeru.
//
// Knihovna navíc: "WebSockets" od Markus Sattler (links2004/arduinoWebSockets)

#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <WebSocketsServer.h>
#include "esp_timer.h"

// ---------------------------------------------------------------- nastavení
static const int      SENSOR_PIN   = 32;              // jen ADC1 (GPIO 32–39), ADC2 nejde používat s Wi-Fi
static const char*    AP_SSID      = "RC_Lap_Timer";
static const char*    AP_PASS      = "";              // prázdné = otevřená síť, jinak min. 8 znaků
static const char*    HOSTNAME     = "laptimer";      // http://laptimer.local
static const int      MAX_LAPS     = 200;
static const uint32_t DEF_MIN_LAP  = 2000;            // ms, kratší průjezdy se ignorují
static const int      DEF_DELTA    = 800;             // o kolik se musí hodnota ADC odchýlit od klidu
static const uint32_t STA_TIMEOUT  = 10000;           // jak dlouho zkoušet domácí Wi-Fi při startu
static const uint32_t LIVE_EVERY   = 200;             // ms, živá data senzoru do prohlížeče

// ---------------------------------------------------------------- stav
struct Config {
  uint32_t minLapMs;
  int      delta;
};

static Config           cfg;
static Preferences      prefs;
static WebServer        server(80);
static WebSocketsServer ws(81);
static DNSServer        dns;
static portMUX_TYPE     mux = portMUX_INITIALIZER_UNLOCKED;

static String staSsid, staPass, staIp;

// sdílené mezi senzorovým taskem a zbytkem (chráněno mux)
static int64_t  lastCrossUs = 0;     // 0 = ještě neodstartováno
static uint32_t laps[MAX_LAPS];
static int      lapCount   = 0;      // celkový počet kol (může přesáhnout MAX_LAPS)
static uint32_t bestMs     = 0;
static uint32_t ignored    = 0;      // průjezdy zahozené ochrannou lhůtou

static volatile int  liveRaw     = 0;
static volatile int  liveBase    = 0;
static volatile bool liveBroken  = false;
static volatile bool calibrateRq = true;   // kalibrace hned po startu
static volatile bool stateDirty  = false;  // nové kolo / reset, poslat všem klientům

static uint32_t restartAt = 0;

// ---------------------------------------------------------------- měření
static void onCrossing(int64_t nowUs) {
  portENTER_CRITICAL(&mux);
  if (lastCrossUs == 0) {
    lastCrossUs = nowUs;                              // první průjezd jen startuje
  } else {
    int64_t diffUs = nowUs - lastCrossUs;
    if (diffUs >= (int64_t)cfg.minLapMs * 1000) {
      uint32_t ms = (uint32_t)((diffUs + 500) / 1000);
      laps[lapCount % MAX_LAPS] = ms;
      lapCount++;
      if (bestMs == 0 || ms < bestMs) bestMs = ms;
      lastCrossUs = nowUs;
    } else {
      ignored++;
    }
  }
  portEXIT_CRITICAL(&mux);
  stateDirty = true;
}

static int readAvg(int samples) {
  long sum = 0;
  for (int i = 0; i < samples; i++) { sum += analogRead(SENSOR_PIN); delay(2); }
  return sum / samples;
}

// Samostatný FreeRTOS task, web ani Wi-Fi měření nezdrží.
static void sensorTask(void*) {
  float base   = 0;
  bool  broken = false;

  for (;;) {
    if (calibrateRq) {
      base = readAvg(64);                             // klidová hodnota s paprskem na senzoru
      broken = false;
      calibrateRq = false;
      Serial.printf("Kalibrace: klid = %d\n", (int)base);
    }

    int     raw = analogRead(SENSOR_PIN);
    int64_t now = esp_timer_get_time();
    int     dev = abs(raw - (int)base);               // funguje pro obě polarity zapojení

    if (!broken) {
      if (dev > cfg.delta) {
        broken = true;
        onCrossing(now);
      } else {
        base += (raw - base) * 0.0005f;               // pomalé sledování okolního světla (~2 s)
      }
    } else if (dev < cfg.delta / 2) {                 // hystereze, paprsek zase volný
      broken = false;
    }

    liveRaw = raw; liveBase = (int)base; liveBroken = broken;
    vTaskDelay(1);                                    // ~1 kHz vzorkování
  }
}

// ---------------------------------------------------------------- JSON
static long currentLapMs(int64_t last) {
  return last ? (long)((esp_timer_get_time() - last) / 1000) : 0;
}

static String buildState() {
  static uint32_t snap[MAX_LAPS];
  int n, count; uint32_t best, ign; int64_t last;

  portENTER_CRITICAL(&mux);
  count = lapCount; best = bestMs; ign = ignored; last = lastCrossUs;
  n = count < MAX_LAPS ? count : MAX_LAPS;
  for (int i = 0; i < n; i++) snap[i] = laps[(count - n + i) % MAX_LAPS];   // od nejstaršího
  portEXIT_CRITICAL(&mux);

  String j;
  j.reserve(320 + n * 8);
  j += "{\"t\":\"state\",\"running\":"; j += last ? "true" : "false";
  j += ",\"cur\":";     j += currentLapMs(last);
  j += ",\"count\":";   j += count;
  j += ",\"best\":";    j += best;
  j += ",\"ignored\":"; j += ign;
  j += ",\"minLap\":";  j += cfg.minLapMs;
  j += ",\"delta\":";   j += cfg.delta;
  j += ",\"ssid\":\"";  j += staSsid; j += '"';      // SSID bez uvozovek, viz handleWifi
  j += ",\"staIp\":\""; j += staIp;   j += '"';
  j += ",\"laps\":[";
  for (int i = 0; i < n; i++) { if (i) j += ','; j += snap[i]; }
  j += "]}";
  return j;
}

static String buildLive() {
  int64_t last;
  portENTER_CRITICAL(&mux);
  last = lastCrossUs;
  portEXIT_CRITICAL(&mux);

  String j = "{\"t\":\"live\",\"raw\":";
  j += liveRaw;
  j += ",\"base\":";   j += liveBase;
  j += ",\"broken\":"; j += liveBroken ? "true" : "false";
  j += ",\"cur\":";    j += currentLapMs(last);
  j += '}';
  return j;
}

// ---------------------------------------------------------------- web
static const char PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="cs"><head>
<meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta name="theme-color" content="#121318"><meta name="apple-mobile-web-app-capable" content="yes">
<title>RC Lap Timer</title>
<style>
:root{--bg:#121318;--card:#1d1f27;--line:#2c2f3a;--fg:#eef0f5;--mut:#8b90a0;--acc:#ff4a5a;--ok:#3ddc84;--gold:#ffd54a}
*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font:16px system-ui,sans-serif;padding:16px;max-width:520px;margin:auto}
h1{font-size:18px;margin:0 0 12px;display:flex;justify-content:space-between;align-items:center}
#conn{font-size:12px;color:var(--mut);display:flex;gap:6px;align-items:center}
.dot{width:10px;height:10px;border-radius:50%;background:var(--acc);display:inline-block}
.card{background:var(--card);border:1px solid var(--line);border-radius:12px;padding:14px;margin-bottom:12px}
.lbl{color:var(--mut);font-size:12px;text-transform:uppercase;letter-spacing:.06em}
.big{font:700 56px ui-monospace,Menlo,monospace;text-align:center;margin:4px 0}
.flash{animation:fl .6s}@keyframes fl{from{background:#2f6b45}to{background:var(--card)}}
.flash.b{animation:flb .9s}@keyframes flb{from{background:#7a6516}to{background:var(--card)}}
.row{display:flex;gap:12px}.row>*{flex:1}.row .card{text-align:center}
.mid{font:700 26px ui-monospace,Menlo,monospace}
.best{color:var(--gold)}
ol{list-style:none;margin:0;padding:0;max-height:40vh;overflow:auto}
li{display:flex;justify-content:space-between;padding:6px 2px;border-bottom:1px solid var(--line);font-family:ui-monospace,Menlo,monospace}
li.b{color:var(--gold)}li span:last-child{color:var(--mut)}
button{background:var(--line);color:var(--fg);border:0;border-radius:8px;padding:10px 14px;font-size:15px}
button.r{background:var(--acc)}
.bar{height:10px;background:var(--line);border-radius:5px;position:relative;margin:8px 0}
.bar i{position:absolute;top:-3px;width:3px;height:16px;border-radius:2px}
input{width:140px;background:var(--bg);color:var(--fg);border:1px solid var(--line);border-radius:6px;padding:6px}
label{display:flex;justify-content:space-between;align-items:center;margin:8px 0;gap:8px}
details{margin-top:4px}details summary{cursor:pointer;color:var(--mut)}
.hint{color:var(--mut);font-size:13px}
</style></head><body>
<h1>🏁 RC Lap Timer <span id="conn"><span class="dot" id="dot"></span><span id="ctxt">připojuji</span></span></h1>

<div class="card" id="curCard"><div class="lbl" id="state">Čekám na první průjezd</div><div class="big" id="cur">--.---</div></div>
<div class="row">
  <div class="card"><div class="lbl">Poslední</div><div class="mid" id="last">--</div><div class="lbl" id="lastd">&nbsp;</div></div>
  <div class="card"><div class="lbl">Nejlepší</div><div class="mid best" id="best">--</div><div class="lbl" id="avg">&nbsp;</div></div>
</div>

<div class="card"><div class="lbl" style="margin-bottom:6px">Kola</div><ol id="laps"></ol></div>

<div class="row" style="margin-bottom:12px">
  <button class="r" onclick="act('reset','Smazat všechna kola?')">Nový trénink</button>
  <button id="snd" onclick="toggleSound()">🔇 Zvuk</button>
</div>

<div class="card"><details><summary>Senzor</summary>
  <p class="lbl">Hodnota ADC <b id="raw">0</b> · klid <b id="base">0</b> · paprsek <b id="beam">?</b></p>
  <div class="bar"><i id="mb" style="background:var(--mut)"></i><i id="mr" style="background:var(--ok)"></i></div>
  <label>Min. čas kola (s) <input id="minLap" type="number" step="0.1" min="0.3"></label>
  <label>Citlivost, odchylka ADC <input id="delta" type="number" step="50" min="50" max="4000"></label>
  <div class="row"><button onclick="save()">Uložit</button><button onclick="act('calibrate')">Kalibrovat klid</button></div>
  <p class="hint">Kalibruj s volným paprskem namířeným na senzor. Zahozených průjezdů <b id="ign">0</b>.</p>
</details></div>

<div class="card"><details><summary>Wi-Fi</summary>
  <p class="hint" id="wstat"></p>
  <label>Domácí síť <input id="ssid" autocomplete="off"></label>
  <label>Heslo <input id="pass" type="password" autocomplete="off"></label>
  <div class="row"><button onclick="wifi(false)">Uložit a restart</button><button onclick="wifi(true)">Zapomenout</button></div>
  <p class="hint">Síť RC_Lap_Timer běží vždy. Když je domácí síť nastavená a v dosahu, timer je navíc na http://laptimer.local, takže telefon nemusí přepínat Wi-Fi a neztratí internet.</p>
</details></div>

<script>
const $=id=>document.getElementById(id);
let S=null,cur=0,at=0,live=null,sound=false,ac=null,seen=-1,cfgLoaded=false,sock=null,pollT=null;
const f=ms=>(ms/1000).toFixed(3);

function toggleSound(){sound=!sound;$('snd').textContent=(sound?'🔊':'🔇')+' Zvuk';
  if(sound&&!ac){ac=new(window.AudioContext||window.webkitAudioContext)()}}
function beep(best){if(navigator.vibrate)navigator.vibrate(best?[80,60,80]:80);if(!sound||!ac)return;
  const o=ac.createOscillator(),g=ac.createGain();o.frequency.value=best?1320:880;g.gain.value=.2;
  o.connect(g);g.connect(ac.destination);o.start();o.stop(ac.currentTime+(best?.35:.15))}

function conn(state){const c={ws:['var(--ok)','živě'],poll:['var(--gold)','záložní režim'],off:['var(--acc)','offline']}[state];
  $('dot').style.background=c[0];$('ctxt').textContent=c[1]}

function onState(s){
  const newLap=seen>=0&&s.count>seen;seen=s.count;S=s;cur=s.cur;at=performance.now();
  const last=s.laps.length?s.laps[s.laps.length-1]:0,isBest=last&&last===s.best;
  if(newLap){beep(isBest);const c=$('curCard');c.className='card';void c.offsetWidth;c.className='card flash'+(isBest?' b':'')}
  $('last').textContent=last?f(last):'--';
  $('best').textContent=s.best?f(s.best):'--';
  $('lastd').textContent=last&&!isBest?'+'+f(last-s.best):' ';
  $('avg').textContent=s.laps.length?'ø '+f(s.laps.reduce((a,b)=>a+b,0)/s.laps.length):' ';
  $('laps').innerHTML=s.laps.map((ms,i)=>[s.count-s.laps.length+i+1,ms]).reverse()
    .map(([n,ms])=>`<li class="${ms===s.best?'b':''}"><span>${n}.</span><span>${f(ms)}</span><span>${ms===s.best?'★':'+'+f(ms-s.best)}</span></li>`).join('');
  $('ign').textContent=s.ignored;
  $('wstat').textContent=s.ssid?(s.staIp?`Připojeno k ${s.ssid}, IP ${s.staIp}`:`Síť ${s.ssid} nebyla při startu v dosahu`):'Domácí síť není nastavená';
  if(!cfgLoaded){$('minLap').value=s.minLap/1000;$('delta').value=s.delta;$('ssid').value=s.ssid;cfgLoaded=true}
}
function onLive(l){live=l;cur=l.cur;at=performance.now();
  $('raw').textContent=l.raw;$('base').textContent=l.base;$('beam').textContent=l.broken?'přerušen':'volný';
  $('mr').style.left=(l.raw/40.95)+'%';$('mb').style.left=(l.base/40.95)+'%'}

// WebSocket s automatickým obnovením, při výpadku záložní dotazování přes HTTP
function connect(){
  try{sock=new WebSocket(`ws://${location.hostname}:81/`)}catch(e){return startPoll()}
  const t=setTimeout(()=>{if(sock.readyState!==1)sock.close()},3000);
  sock.onopen=()=>{clearTimeout(t);stopPoll();conn('ws')};
  sock.onmessage=e=>{const m=JSON.parse(e.data);m.t==='state'?onState(m):onLive(m)};
  sock.onclose=()=>{clearTimeout(t);startPoll();setTimeout(connect,2000)};
}
async function poll(){try{const r=await fetch('/api/state',{cache:'no-store'});onState(await r.json());conn('poll')}catch(e){conn('off')}}
function startPoll(){if(!pollT){conn('off');poll();pollT=setInterval(poll,1000)}}
function stopPoll(){clearInterval(pollT);pollT=null}

async function act(a,q){if(q&&!confirm(q))return;await fetch('/api/'+a,{method:'POST'});if(pollT)poll()}
async function save(){const p=new URLSearchParams({minLap:Math.round($('minLap').value*1000),delta:$('delta').value});
  await fetch('/api/config?'+p,{method:'POST'});cfgLoaded=false;if(pollT)poll()}
async function wifi(forget){
  if(forget&&!confirm('Zapomenout domácí Wi-Fi?'))return;
  const p=new URLSearchParams(forget?{ssid:'',pass:''}:{ssid:$('ssid').value,pass:$('pass').value});
  await fetch('/api/wifi?'+p,{method:'POST'});
  $('wstat').textContent='Restartuji… připoj se znovu za pár sekund.'}

function tick(){
  if(S&&S.running){$('cur').textContent=f(cur+(performance.now()-at));$('state').textContent='Kolo '+(S.count+1)}
  else{$('cur').textContent='--.---';$('state').textContent='Čekám na první průjezd'}
  requestAnimationFrame(tick)}
connect();tick();
</script></body></html>)HTML";

static void sendOk() { server.send(200, "application/json", "{\"ok\":true}"); }

static void handleReset() {
  portENTER_CRITICAL(&mux);
  lastCrossUs = 0; lapCount = 0; bestMs = 0; ignored = 0;
  portEXIT_CRITICAL(&mux);
  stateDirty = true;
  sendOk();
}

static void handleCalibrate() {
  calibrateRq = true;
  sendOk();
}

static void handleConfig() {
  if (server.hasArg("minLap")) cfg.minLapMs = constrain(server.arg("minLap").toInt(), 300, 600000);
  if (server.hasArg("delta"))  cfg.delta    = constrain(server.arg("delta").toInt(), 50, 4000);
  prefs.putUInt("minLap", cfg.minLapMs);
  prefs.putInt("delta", cfg.delta);
  stateDirty = true;
  sendOk();
}

static void handleWifi() {
  String ssid = server.arg("ssid");
  ssid.replace("\"", ""); ssid.replace("\\", "");     // ať to nerozbije JSON
  ssid.trim();
  prefs.putString("ssid", ssid);
  prefs.putString("pass", server.arg("pass"));
  sendOk();
  restartAt = millis() + 800;                         // nech odpověď odejít, pak restart
}

// Captive portal: telefon zkouší adresy typu /generate_204 nebo /hotspot-detect.html.
// Na všechno neznámé odpovíme přesměrováním na stránku timeru, telefon ji pak sám otevře.
static void handleNotFound() {
  String host = server.hostHeader();
  bool ours = host == WiFi.softAPIP().toString() || (staIp.length() && host == staIp)
           || host.startsWith(HOSTNAME);
  if (!ours) {
    server.sendHeader("Location", String("http://") + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Nenalezeno");
  }
}

static void onWsEvent(uint8_t num, WStype_t type, uint8_t*, size_t) {
  if (type == WStype_CONNECTED) {
    String s = buildState();
    ws.sendTXT(num, s);
  }
}

// ---------------------------------------------------------------- Wi-Fi
static void setupWifi() {
  staSsid = prefs.getString("ssid", "");
  staPass = prefs.getString("pass", "");

  WiFi.persistent(false);
  WiFi.setHostname(HOSTNAME);

  if (staSsid.length()) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.begin(staSsid.c_str(), staPass.c_str());
    Serial.printf("Připojuji k '%s'", staSsid.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < STA_TIMEOUT) { delay(250); Serial.print('.'); }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
      staIp = WiFi.localIP().toString();
      WiFi.setAutoReconnect(true);
      Serial.printf("Domácí Wi-Fi OK, IP %s\n", staIp.c_str());
    } else {
      // Neúspěšné hledání sítě přeskakuje kanály a ruší AP, takže STA vypneme.
      Serial.println("Domácí Wi-Fi nedostupná, běží jen vlastní síť");
      WiFi.disconnect(true);
      WiFi.mode(WIFI_AP);
    }
  } else {
    WiFi.mode(WIFI_AP);
  }

  if (strlen(AP_PASS) >= 8) WiFi.softAP(AP_SSID, AP_PASS);
  else                      WiFi.softAP(AP_SSID);
  WiFi.setSleep(false);                               // nižší latence, ESP32 je stejně na powerbance

  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());                // všechny domény na nás = captive portal

  if (MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
  }

  Serial.printf("Wi-Fi '%s', http://%s", AP_SSID, WiFi.softAPIP().toString().c_str());
  if (staIp.length()) Serial.printf(" nebo http://%s.local (%s)", HOSTNAME, staIp.c_str());
  Serial.println();
}

// ---------------------------------------------------------------- setup / loop
void setup() {
  Serial.begin(115200);

  prefs.begin("laptimer", false);
  cfg.minLapMs = prefs.getUInt("minLap", DEF_MIN_LAP);
  cfg.delta    = prefs.getInt("delta", DEF_DELTA);

  analogReadResolution(12);
  analogSetPinAttenuation(SENSOR_PIN, ADC_11db);      // celý rozsah 0–3,3 V

  setupWifi();

  server.on("/",              HTTP_GET,  []() { server.send(200, "text/html", PAGE); });
  server.on("/api/state",     HTTP_GET,  []() { server.send(200, "application/json", buildState()); });
  server.on("/api/reset",     HTTP_POST, handleReset);
  server.on("/api/calibrate", HTTP_POST, handleCalibrate);
  server.on("/api/config",    HTTP_POST, handleConfig);
  server.on("/api/wifi",      HTTP_POST, handleWifi);
  server.onNotFound(handleNotFound);
  server.begin();

  ws.begin();
  ws.onEvent(onWsEvent);

  xTaskCreatePinnedToCore(sensorTask, "sensor", 4096, nullptr, 5, nullptr, 1);
}

void loop() {
  static uint32_t lastLive = 0;

  dns.processNextRequest();
  server.handleClient();
  ws.loop();

  if (stateDirty) {                                   // nové kolo jde ven okamžitě
    stateDirty = false;
    String s = buildState();
    ws.broadcastTXT(s);
  }

  if (millis() - lastLive >= LIVE_EVERY && ws.connectedClients() > 0) {
    lastLive = millis();
    String l = buildLive();
    ws.broadcastTXT(l);
  }

  if (restartAt && (int32_t)(millis() - restartAt) >= 0) ESP.restart();

  delay(1);
}
