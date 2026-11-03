// JSON messages for the web app: {"t":"state"}, {"t":"live"} and the Wi-Fi scan results.
// web.cpp collects the values from the other modules, this module only writes the text.
// No hardware dependencies, unit-tested on the host (test/test_webjson).
#pragma once
#include <stdint.h>
#include <string>
#include "config.h"
#include "laps.h"
#include "sensor.h"
#include "timing.h"

struct JsonDriver {
  uint8_t     id;
  const char* name;
};

struct JsonCar {                 // a used car slot
  int         slot;
  uint16_t    tx;
  const char* name;
};

struct StateInput {
  int64_t            nowUs;
  bool               multi;
  RaceInfo           race;
  uint16_t           setLaps, setMin;      // last race setup
  uint32_t           unidentified;
  uint32_t           minLapMs;
  int                delta;
  bool               zroundOn, zroundConnected, zroundRacing;
  const char*        ssid;
  const char*        staIp;
  const char*        sta;
  const char*        staErr;
  bool               hasPass;
  const JsonDriver*  drivers;
  int                nDrivers;
  const JsonCar*     cars;
  int                nCars;
  const LapSnapshot* laps;
};

struct LiveInput {
  SensorLive sensor;
  long       cur[MAX_CARS];              // lap in progress per car slot
  int        rssi;
};

struct JsonNetwork {
  const char* ssid;
  int         rssi;
  bool        open;
};

void webjsonState(std::string& j, const StateInput& in);
void webjsonLive(std::string& j, const LiveInput& in);
void webjsonScan(std::string& j, bool scanning, const JsonNetwork* nets, int n);
