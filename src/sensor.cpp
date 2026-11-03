#include "sensor.h"
#include <Arduino.h>
#include "esp_timer.h"
#include "soc/gpio_reg.h"
#include "beam.h"
#include "blink.h"
#include "config.h"
#include "log.h"
#include "nec.h"
#include "settings.h"

static_assert(IR_PIN >= 32 && IR_PIN <= 39, "the IR ISR reads GPIO_IN1_REG, the pin must be 32–39");

// ---------------------------------------------------------------- IR receiver edges (ISR → task)
static constexpr uint32_t IR_BUF = 512;              // ~7 NEC frames of edges
static int64_t           irTime[IR_BUF];
static uint8_t           irLevel[IR_BUF];
static volatile uint32_t irHead = 0, irTail = 0;     // single producer (ISR), single consumer (task)

static void IRAM_ATTR irIsr() {
  uint32_t h = irHead, next = (h + 1) % IR_BUF;
  if (next == irTail) return;                         // full: drop, the decoders resync
  irTime[h]  = esp_timer_get_time();
  irLevel[h] = (REG_READ(GPIO_IN1_REG) >> (IR_PIN - 32)) & 1;
  irHead = next;
}

// ---------------------------------------------------------------- task state
static Beam          beam;
static NecDecoder    nec;
static BlinkDecoder  blinkDec;
static BlinkIds      blinkIds;
static PassMatcher   matcher;
static QueueHandle_t passings;

static volatile bool     calibrateRq = true;          // calibrate right after start
static volatile bool     multiRq     = false;
static volatile bool     resetRq     = false;

static volatile int      liveRaw = 0, liveBase = 0, liveNoise = 0, livePeak = 0;
static volatile bool     liveBroken = false;
static volatile uint16_t liveIrId = 0;
static volatile int64_t  liveIrUs = 0;
static volatile uint32_t liveUnidentified = 0;

static int readAvg(int samples) {
  long sum = 0;
  for (int i = 0; i < samples; i++) { sum += analogRead(SENSOR_PIN); delay(2); }
  return sum / samples;
}

static void seen(uint16_t id, int64_t tUs) {
  if (!id) return;
  matcher.ir(id, tUs);
  liveIrId = id;
  liveIrUs = tUs;
}

static void drainIr() {
  while (irTail != irHead) {
    uint32_t t = irTail;
    bool mark = irLevel[t] == 0;                      // receiver output is low while it sees the carrier
    int64_t at = irTime[t];
    irTail = (t + 1) % IR_BUF;

    uint32_t code;
    if (nec.edge(mark, at, code)) seen(necTransponderId(code), at);
    uint16_t hz;
    if (blinkDec.edge(mark, at, hz)) seen(blinkIds.identify(hz), at);
  }
}

// Separate task on core 1, so neither the web nor Wi-Fi delays the measurement.
static void sensorTask(void*) {
  bool led = false;
  Passing out[8];

  for (;;) {
    if (calibrateRq) {
      beam.calibrate(readAvg(64));                    // idle value with the beam on the sensor
      calibrateRq = false;
      LOGF("Calibration: idle = %d\n", beam.base());
    }
    if (multiRq != matcher.multi()) matcher.setMulti(multiRq);
    if (resetRq) { matcher.resetCounters(); resetRq = false; }

    int     raw = analogRead(SENSOR_PIN);
    int64_t now = esp_timer_get_time();
    if (beam.update(raw, settings.delta, (uint32_t)(now / 1000))) matcher.beam(now);
    drainIr();

    int n = matcher.poll(now, out, 8);
    for (int i = 0; i < n; i++) xQueueSend(passings, &out[i], 0);

    if (beam.broken() != led) {                       // LED shows the beam state, handy for aiming
      led = beam.broken();
      digitalWrite(LED_PIN, led ? HIGH : LOW);
    }
    liveRaw = raw; liveBase = beam.base(); liveBroken = beam.broken();
    liveNoise = beam.noise(); livePeak = beam.peak();
    liveUnidentified = matcher.unidentified();
    vTaskDelay(1);                                    // ~1 kHz sampling
  }
}

void sensorBegin(const uint16_t* knownBlink, int nKnownBlink) {
  for (int i = 0; i < nKnownBlink; i++) blinkIds.add(knownBlink[i]);
  passings = xQueueCreate(16, sizeof(Passing));

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  analogReadResolution(12);
  analogSetPinAttenuation(SENSOR_PIN, ADC_11db);      // full 0–3.3 V range
  pinMode(IR_PIN, INPUT_PULLUP);                      // the TSOP output idles high
  attachInterrupt(digitalPinToInterrupt(IR_PIN), irIsr, CHANGE);
  xTaskCreatePinnedToCore(sensorTask, "sensor", 4096, nullptr, 5, nullptr, 1);
}

void sensorCalibrate()             { calibrateRq = true; }
void sensorSetMulti(bool multi)    { multiRq = multi; }
void sensorResetCounters()         { resetRq = true; }
bool sensorNextPassing(Passing& p) { return xQueueReceive(passings, &p, 0) == pdTRUE; }

SensorLive sensorLive() {
  int noise = liveNoise, peak = livePeak;
  int64_t irUs = liveIrUs;
  long ago = irUs ? (long)((esp_timer_get_time() - irUs) / 1000) : -1;
  return { liveRaw, liveBase, liveBroken, noise, peak, beamSuggestDelta(noise, peak),
           liveIrId, ago, liveUnidentified };
}
