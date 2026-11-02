#include "sensor.h"
#include <Arduino.h>
#include "esp_timer.h"
#include "config.h"
#include "laps.h"
#include "settings.h"

static volatile int  liveRaw     = 0;
static volatile int  liveBase    = 0;
static volatile bool liveBroken  = false;
static volatile bool calibrateRq = true;   // calibrate right after start

static int readAvg(int samples) {
  long sum = 0;
  for (int i = 0; i < samples; i++) { sum += analogRead(SENSOR_PIN); delay(2); }
  return sum / samples;
}

// Separate task on core 1, so neither the web nor Wi-Fi delays the measurement.
static void sensorTask(void*) {
  float base   = 0;
  bool  broken = false;

  for (;;) {
    if (calibrateRq) {
      base = readAvg(64);                             // idle value with the beam on the sensor
      broken = false;
      calibrateRq = false;
      Serial.printf("Calibration: idle = %d\n", (int)base);
    }

    int     raw   = analogRead(SENSOR_PIN);
    int64_t now   = esp_timer_get_time();
    int     dev   = abs(raw - (int)base);             // works for either wiring polarity
    int     delta = settings.delta;

    if (!broken) {
      if (dev > delta) {
        broken = true;
        lapsCrossing(now, settings.minLapMs);
      } else {
        base += (raw - base) * 0.0005f;               // slowly track ambient light (~2 s)
      }
    } else if (dev < delta / 2) {                     // hysteresis, beam is clear again
      broken = false;
    }

    liveRaw = raw; liveBase = (int)base; liveBroken = broken;
    vTaskDelay(1);                                    // ~1 kHz sampling
  }
}

void sensorBegin() {
  analogReadResolution(12);
  analogSetPinAttenuation(SENSOR_PIN, ADC_11db);      // full 0–3.3 V range
  xTaskCreatePinnedToCore(sensorTask, "sensor", 4096, nullptr, 5, nullptr, 1);
}

void sensorCalibrate() { calibrateRq = true; }

SensorLive sensorLive() { return { liveRaw, liveBase, liveBroken }; }
