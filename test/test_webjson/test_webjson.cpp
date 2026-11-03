// Host unit tests for src/webjson.cpp: pio test -e native
// The expected texts were produced by the JSON code as it was in web.cpp before it moved here,
// so these tests pin the format the app (web/app.js, web/race.js) and tools/mock_server.py rely on.
#include <string.h>
#include <unity.h>
#include "webjson.h"

static LapSnapshot snap;
static std::string out;

static const int64_t NOW = 123456789012LL;

void setUp() {
  for (int c = 0; c < MAX_CARS; c++) lapsSetDriver(c, 0);
  lapsReset();
}
void tearDown() {}

// Laps of car 1 (driver 1, the second one deleted), car 3 (one IR-only lap) and car 0 (started only).
static void session() {
  lapsSetDriver(1, 1);
  lapsCrossing(1, 1000000, 2000); lapsCrossing(1, 9000000, 2000); lapsCrossing(1, 12000000, 2000);
  lapsCrossing(3, 2000000, 2000); lapsCrossing(3, 10500000, 2000, false);
  lapsCrossing(1, 20000000, 2000); lapsDelete(2);
  lapsCrossing(0, 3000000, 2000);
}

static SensorLive sensor() { return { 3000, 2990, false, 25, 2400, 900, 0x1B39, 300, 4 }; }

void test_state() {
  session();
  lapsSnapshot(snap, NOW);
  JsonDriver drivers[] = { { 1, "Mar\\tin" }, { 3, "Jana" } };
  JsonCar cars[] = { { 0, 0, "" }, { 1, 0x1B39, "Red" }, { 3, 465, "" } };

  StateInput in = {};
  in.nowUs = NOW;
  in.multi = true;
  in.race = { RaceState::Running, 120000000000LL, 10, 0 };
  in.setLaps = 12;
  in.setMin = 0;
  in.unidentified = 4;
  in.minLapMs = 2500;
  in.delta = 800;
  in.zroundOn = false;
  in.zroundConnected = true;
  in.zroundRacing = false;
  in.ssid = "Ho\"me\x01";
  in.staIp = "192.168.1.5";
  in.sta = "connected";
  in.staErr = "";
  in.hasPass = true;
  in.drivers = drivers;
  in.nDrivers = 2;
  in.cars = cars;
  in.nCars = 3;
  in.laps = &snap;
  webjsonState(out, in);

  TEST_ASSERT_EQUAL_STRING(
    "{\"t\":\"state\",\"now\":123456789,\"multi\":true,"
    "\"race\":{\"state\":\"running\",\"go\":120000000,\"laps\":10,\"minutes\":0,\"setLaps\":12,\"setMin\":0},"
    "\"count\":4,\"best\":8000,\"ignored\":0,\"unident\":4,\"minLap\":2500,\"delta\":800,"
    "\"zround\":{\"on\":false,\"connected\":true,\"racing\":false},"
    "\"ssid\":\"Ho\\\"me\\u0001\",\"staIp\":\"192.168.1.5\",\"sta\":\"connected\",\"staErr\":\"\",\"hasPass\":true,"
    "\"drivers\":[{\"id\":1,\"name\":\"Mar\\\\tin\"},{\"id\":3,\"name\":\"Jana\"}],"
    "\"cars\":[{\"slot\":0,\"tx\":0,\"name\":\"\",\"driver\":0,\"running\":true,\"cur\":123453789,\"canUndo\":true,\"laps\":0},"
    "{\"slot\":1,\"tx\":6969,\"name\":\"Red\",\"driver\":1,\"running\":true,\"cur\":123436789,\"canUndo\":true,\"laps\":3},"
    "{\"slot\":3,\"tx\":465,\"name\":\"\",\"driver\":0,\"running\":true,\"cur\":123446289,\"canUndo\":true,\"laps\":1}],"
    "\"stats\":[{\"key\":1,\"count\":2,\"best\":8000,\"total\":16000},{\"key\":259,\"count\":1,\"best\":8500,\"total\":8500}],"
    "\"laps\":[[8000,1,0,0,1,1,1,9000],[3000,1,1,1,1,2,1,12000],[8500,0,0,0,3,1,0,10500],[8000,1,0,1,1,3,1,20000]]}",
    out.c_str());
}

void test_state_empty() {
  lapsSnapshot(snap, NOW);
  JsonCar cars[] = { { 0, 0, "" } };
  StateInput in = {};
  in.race = { RaceState::Off, 0, 0, 0 };
  in.ssid = in.staIp = in.sta = in.staErr = "";
  in.cars = cars;
  in.nCars = 1;
  in.laps = &snap;
  webjsonState(out, in);
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), "\"race\":{\"state\":\"off\","));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), "\"drivers\":[],\"cars\":[{\"slot\":0,"));
  TEST_ASSERT_NOT_NULL(strstr(out.c_str(), "\"stats\":[],\"laps\":[]}"));
}

void test_live() {
  session();
  LiveInput in;
  in.sensor = sensor();
  for (int c = 0; c < MAX_CARS; c++) in.cur[c] = lapsCurrentMs(c, NOW);
  in.rssi = -55;
  webjsonLive(out, in);
  TEST_ASSERT_EQUAL_STRING(
    "{\"t\":\"live\",\"raw\":3000,\"base\":2990,\"broken\":false,"
    "\"cur\":[123453789,123436789,0,123446289,0,0,0,0],"
    "\"noise\":25,\"peak\":2400,\"suggest\":900,\"irId\":6969,\"irAgo\":300,\"unident\":4,\"rssi\":-55}",
    out.c_str());
}

void test_scan() {
  JsonNetwork nets[] = { { "Home", -52, false }, { "Cafe \"Free\"", -83, true } };
  webjsonScan(out, false, nets, 2);
  TEST_ASSERT_EQUAL_STRING(
    "{\"scanning\":false,\"networks\":[{\"ssid\":\"Home\",\"rssi\":-52,\"open\":false},"
    "{\"ssid\":\"Cafe \\\"Free\\\"\",\"rssi\":-83,\"open\":true}]}",
    out.c_str());
  webjsonScan(out, true, nets, 0);
  TEST_ASSERT_EQUAL_STRING("{\"scanning\":true,\"networks\":[]}", out.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_state);
  RUN_TEST(test_state_empty);
  RUN_TEST(test_live);
  RUN_TEST(test_scan);
  return UNITY_END();
}
