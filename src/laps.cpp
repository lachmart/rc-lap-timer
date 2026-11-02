#include "laps.h"
#include <Arduino.h>
#include "esp_timer.h"

static portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;

static int64_t  lastCrossUs = 0;     // 0 = not started yet
static uint32_t laps[MAX_LAPS];      // ring buffer
static int      lapCount = 0;
static uint32_t bestMs   = 0;
static uint32_t ignored  = 0;
static volatile uint32_t revision = 0;

void lapsCrossing(int64_t nowUs, uint32_t minLapMs) {
  portENTER_CRITICAL(&mux);
  if (lastCrossUs == 0) {
    lastCrossUs = nowUs;                              // the first pass only starts timing
  } else {
    int64_t diffUs = nowUs - lastCrossUs;
    if (diffUs >= (int64_t)minLapMs * 1000) {
      uint32_t ms = (uint32_t)((diffUs + 500) / 1000);
      laps[lapCount % MAX_LAPS] = ms;
      lapCount++;
      if (bestMs == 0 || ms < bestMs) bestMs = ms;
      lastCrossUs = nowUs;
    } else {
      ignored++;
    }
  }
  revision = revision + 1;                          // ++ on volatile is deprecated in C++20
  portEXIT_CRITICAL(&mux);
}

void lapsReset() {
  portENTER_CRITICAL(&mux);
  lastCrossUs = 0; lapCount = 0; bestMs = 0; ignored = 0;
  revision = revision + 1;                          // ++ on volatile is deprecated in C++20
  portEXIT_CRITICAL(&mux);
}

static long sinceMs(int64_t last) {
  return last ? (long)((esp_timer_get_time() - last) / 1000) : 0;
}

void lapsSnapshot(LapSnapshot& out) {
  portENTER_CRITICAL(&mux);
  int64_t last = lastCrossUs;
  out.count   = lapCount;
  out.bestMs  = bestMs;
  out.ignored = ignored;
  out.n = lapCount < MAX_LAPS ? lapCount : MAX_LAPS;
  for (int i = 0; i < out.n; i++) out.laps[i] = laps[(lapCount - out.n + i) % MAX_LAPS];
  portEXIT_CRITICAL(&mux);

  out.running = last != 0;
  out.curMs   = sinceMs(last);
}

long lapsCurrentMs() {
  portENTER_CRITICAL(&mux);
  int64_t last = lastCrossUs;
  portEXIT_CRITICAL(&mux);
  return sinceMs(last);
}

uint32_t lapsRevision() { return revision; }
