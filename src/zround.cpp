#include "zround.h"
#include <Arduino.h>
#include "esp_timer.h"
#include "log.h"
#include "settings.h"

volatile bool logEnabled = true;

static bool     connected = false;
static bool     racing    = false;
static int64_t  startUs   = 0;
static int64_t  nextTimeUs = 0;
static int      acksLeft  = 0;
static uint32_t nextAckMs = 0;
static char     msg[8];
static int      msgLen    = -1;                       // -1 = waiting for '%'

static void open() {
  logEnabled = !settings.zround;
  connected = racing = false;
  Serial.begin(settings.zround ? 19200 : 115200);
}

void zroundBegin() { open(); }

void zroundApply() {
  Serial.flush();
  Serial.end();
  open();
}

static void handle(char cmd) {
  switch (cmd) {
    case 'C':                                         // connect: RiCino answers %A& 21 times, 100 ms apart
      connected = true;
      acksLeft = 21;
      nextAckMs = millis();
      break;
    case 'I':                                         // race start
      racing = true;
      startUs = esp_timer_get_time();
      nextTimeUs = startUs + 5000000;
      break;
    case 'F':                                         // race finish
      racing = false;
      break;
  }
}

void zroundLoop() {
  if (!settings.zround) return;

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '%') { msgLen = 0; continue; }
    if (msgLen < 0) continue;
    if (c == '&') { if (msgLen > 0) handle(msg[0]); msgLen = -1; continue; }
    if (msgLen < (int)sizeof msg) msg[msgLen++] = c;
    else msgLen = -1;
  }

  if (acksLeft > 0 && (int32_t)(millis() - nextAckMs) >= 0) {
    Serial.print("%A&");
    acksLeft--;
    nextAckMs += 100;
  }

  int64_t now = esp_timer_get_time();
  if (racing && now >= nextTimeUs) {
    Serial.printf("%%T%lX&\r\n", (unsigned long)((nextTimeUs - startUs) / 1000));
    nextTimeUs += 5000000;
  }
}

void zroundPassing(uint16_t id, int64_t tUs) {
  if (!settings.zround || !racing || id == 0 || tUs < startUs) return;
  Serial.printf("%%L%X,%lX&", id, (unsigned long)((tUs - startUs) / 1000));
}

bool zroundConnected() { return connected; }
bool zroundRacing()    { return racing; }
