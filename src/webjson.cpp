#include "webjson.h"
#include <stdio.h>

// std::string += on a small integer type appends a character, so every number goes through num().
static void num(std::string& j, long long v) { j += std::to_string(v); }

static void boolean(std::string& j, bool b) { j += b ? "true" : "false"; }

static void str(std::string& j, const char* s) {
  j += '"';
  for (; *s; s++) {
    char c = *s;
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

static const char* heatStateName(HeatState h) {
  switch (h) {
    case HeatState::Waiting:   return "waiting";
    case HeatState::Countdown: return "countdown";
    default:                   return "running";
  }
}

static void addRace(std::string& j, const StateInput& in) {
  j += ",\"race\":{\"state\":\""; j += raceStateName(in.race.state);
  j += "\",\"go\":";    num(j, in.race.goUs / 1000);
  j += ",\"laps\":";    num(j, in.race.laps);
  j += ",\"minutes\":"; num(j, in.race.minutes);
  j += ",\"order\":[";                               // heats: driver ids in turn
  for (int i = 0; i < in.race.nHeats; i++) { if (i) j += ','; num(j, in.race.order[i]); }
  j += "],\"heat\":";  num(j, in.race.heat);
  j += ",\"heatState\":\""; j += heatStateName(in.race.heatState);
  j += "\",\"heatGo\":"; num(j, in.race.heatGoUs / 1000);
  j += ",\"prep\":";  num(j, in.race.prepSec);
  j += ",\"setLaps\":"; num(j, in.setLaps);
  j += ",\"setMin\":";  num(j, in.setMin);
  j += '}';
}

static void addDrivers(std::string& j, const StateInput& in) {
  j += ",\"drivers\":[";
  for (int i = 0; i < in.nDrivers; i++) {
    if (i) j += ',';
    j += "{\"id\":"; num(j, in.drivers[i].id);
    j += ",\"name\":"; str(j, in.drivers[i].name);
    j += '}';
  }
  j += ']';
}

static void addCars(std::string& j, const StateInput& in) {
  j += ",\"cars\":[";
  for (int i = 0; i < in.nCars; i++) {
    const JsonCar& car = in.cars[i];
    const CarState& cs = in.laps->cars[car.slot];
    if (i) j += ',';
    j += "{\"slot\":";    num(j, car.slot);
    j += ",\"tx\":";      num(j, car.tx);
    j += ",\"name\":";    str(j, car.name);
    j += ",\"driver\":";  num(j, cs.driver);
    j += ",\"running\":"; boolean(j, cs.running);
    j += ",\"cur\":";     num(j, cs.curMs);
    j += ",\"canUndo\":"; boolean(j, cs.canUndo);
    j += ",\"laps\":";    num(j, cs.laps);
    j += '}';
  }
  j += ']';
}

static void addStats(std::string& j, const LapSnapshot& s) {
  j += ",\"stats\":[";
  for (int i = 0; i < s.nStats; i++) {
    const DriverStats& d = s.stats[i];
    if (i) j += ',';
    j += "{\"key\":";    num(j, d.key);
    j += ",\"count\":";  num(j, d.count);
    j += ",\"best\":";   num(j, d.bestMs);
    j += ",\"total\":";  num(j, d.totalMs);
    j += '}';
  }
  j += ']';
}

// [ms, driver, deleted, continues the car's previous lap, car, lap number of the car, precise, end ms],
// oldest first; read by the app through L in web/race.js
static void addLaps(std::string& j, const LapSnapshot& s) {
  j += ",\"laps\":[";
  int prevOfCar[MAX_CARS];
  for (int c = 0; c < MAX_CARS; c++) prevOfCar[c] = -1;
  for (int i = 0; i < s.n; i++) {
    const Lap& l = s.laps[i];
    bool cont = prevOfCar[l.car] >= 0 && lapsContinues(s.laps[prevOfCar[l.car]], l);
    prevOfCar[l.car] = i;
    if (i) j += ',';
    j += '['; num(j, l.ms); j += ','; num(j, l.driver);
    j += ','; j += l.deleted ? '1' : '0';
    j += ','; j += cont ? '1' : '0';
    j += ','; num(j, l.car);
    j += ','; num(j, l.num);
    j += ','; j += l.precise ? '1' : '0';
    j += ','; num(j, l.endUs / 1000);
    j += ']';
  }
  j += ']';
}

void webjsonState(std::string& j, const StateInput& in) {
  const LapSnapshot& s = *in.laps;
  j.clear();
  j.reserve(1200 + s.n * 44);
  j += "{\"t\":\"state\",\"now\":"; num(j, in.nowUs / 1000);   // ms; client converts the other times with it
  j += ",\"multi\":";   boolean(j, in.multi);
  addRace(j, in);
  j += ",\"count\":";   num(j, s.count);
  j += ",\"best\":";    num(j, s.bestMs);
  j += ",\"ignored\":"; num(j, s.ignored);
  j += ",\"unident\":"; num(j, in.unidentified);
  j += ",\"minLap\":";  num(j, in.minLapMs);
  j += ",\"delta\":";   num(j, in.delta);
  j += ",\"zround\":{\"on\":"; boolean(j, in.zroundOn);
  j += ",\"connected\":"; boolean(j, in.zroundConnected);
  j += ",\"racing\":";  boolean(j, in.zroundRacing);
  j += "},\"ssid\":";   str(j, in.ssid);
  j += ",\"staIp\":";   str(j, in.staIp);
  j += ",\"sta\":";     str(j, in.sta);
  j += ",\"staErr\":";  str(j, in.staErr);
  j += ",\"hasPass\":"; boolean(j, in.hasPass);
  addDrivers(j, in);
  addCars(j, in);
  addStats(j, s);
  addLaps(j, s);
  j += '}';
}

void webjsonLive(std::string& j, const LiveInput& in) {
  const SensorLive& l = in.sensor;
  j.clear();
  j += "{\"t\":\"live\",\"raw\":"; num(j, l.raw);
  j += ",\"base\":";    num(j, l.base);
  j += ",\"broken\":";  boolean(j, l.broken);
  j += ",\"cur\":[";                   // lap in progress per car slot
  for (int c = 0; c < MAX_CARS; c++) { if (c) j += ','; num(j, in.cur[c]); }
  j += "],\"noise\":";  num(j, l.noise);
  j += ",\"peak\":";    num(j, l.peak);
  j += ",\"suggest\":"; num(j, l.suggest);
  j += ",\"irId\":";    num(j, l.irId);
  j += ",\"irAgo\":";   num(j, l.irAgoMs);
  j += ",\"unident\":"; num(j, l.unidentified);
  j += ",\"rssi\":";    num(j, in.rssi);
  j += '}';
}

void webjsonScan(std::string& j, bool scanning, const JsonNetwork* nets, int n) {
  j.clear();
  j += "{\"scanning\":"; boolean(j, scanning);
  j += ",\"networks\":[";
  for (int i = 0; i < n; i++) {
    if (i) j += ',';
    j += "{\"ssid\":"; str(j, nets[i].ssid);
    j += ",\"rssi\":"; num(j, nets[i].rssi);
    j += ",\"open\":"; boolean(j, nets[i].open);
    j += '}';
  }
  j += "]}";
}
