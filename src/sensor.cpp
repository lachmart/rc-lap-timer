#include "sensor.h"
#include <Arduino.h>
#include "esp_timer.h"
#include "beam.h"
#include "config.h"
#include "laps.h"
#include "settings.h"

static Beam beam;                          // owned by the sensor task
static volatile int  liveRaw     = 0;
static volatile int  liveBase    = 0;
static volatile bool liveBroken  = false;
static volatile int  liveNoise   = 0;
static volatile int  livePeak    = 0;
static volatile bool calibrateRq = true;   // calibrate right after start

static int readAvg(int samples) {
  long sum = 0;
  for (int i = 0; i < samples; i++) { sum += analogRead(SENSOR_PIN); delay(2); }
  return sum / samples;
}

// Separate task on core 1, so neither the web nor Wi-Fi delays the measurement.
static void sensorTask(void*) {
  bool led = false;

  for (;;) {
    if (calibrateRq) {
      beam.calibrate(readAvg(64));                    // idle value with the beam on the sensor
      calibrateRq = false;
      Serial.printf("Calibration: idle = %d\n", beam.base());
    }

    int     raw = analogRead(SENSOR_PIN);
    int64_t now = esp_timer_get_time();
    if (beam.update(raw, settings.delta, (uint32_t)(now / 1000))) lapsCrossing(now, settings.minLapMs);

    if (beam.broken() != led) {                       // LED shows the beam state, handy for aiming
      led = beam.broken();
      digitalWrite(LED_PIN, led ? HIGH : LOW);
    }
    liveRaw = raw; liveBase = beam.base(); liveBroken = beam.broken();
    liveNoise = beam.noise(); livePeak = beam.peak();
    vTaskDelay(1);                                    // ~1 kHz sampling
  }
}

void sensorBegin() {
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);
  analogReadResolution(12);
  analogSetPinAttenuation(SENSOR_PIN, ADC_11db);      // full 0–3.3 V range
  xTaskCreatePinnedToCore(sensorTask, "sensor", 4096, nullptr, 5, nullptr, 1);
}

void sensorCalibrate() { calibrateRq = true; }

SensorLive sensorLive() {
  int noise = liveNoise, peak = livePeak;
  return { liveRaw, liveBase, liveBroken, noise, peak, beamSuggestDelta(noise, peak) };
}
