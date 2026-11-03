#include "zround.h"
#include <Arduino.h>
#include <string>
#include "esp_timer.h"
#include "log.h"
#include "settings.h"
#include "zroundproto.h"

volatile bool logEnabled = true;

static ZRoundProto proto;

static void open() {
  logEnabled = !settings.zround;
  proto = ZRoundProto();
  Serial.begin(settings.zround ? 19200 : 115200);
}

static void send(const std::string& s) {
  if (!s.empty()) Serial.write((const uint8_t*)s.data(), s.size());
}

void zroundBegin() { open(); }

void zroundApply() {
  Serial.flush();
  Serial.end();
  open();
}

void zroundLoop() {
  if (!settings.zround) return;
  int64_t now = esp_timer_get_time();
  while (Serial.available()) proto.feed(Serial.read(), now);
  std::string out;
  proto.poll(now, out);
  send(out);
}

void zroundPassing(uint16_t id, int64_t tUs) {
  if (!settings.zround) return;
  std::string out;
  proto.passing(id, tUs, out);
  send(out);
}

bool zroundConnected() { return proto.connected(); }
bool zroundRacing()    { return proto.racing(); }
