// Laser beam gate: reads the LDR in a separate FreeRTOS task and detects crossings
#pragma once

struct SensorLive {
  int  raw;      // latest ADC value
  int  base;     // idle value (beam on the sensor)
  bool broken;   // the beam is currently interrupted
};

void       sensorBegin();        // sets up the ADC and starts the task (calibrates right after start)
void       sensorCalibrate();    // request a new idle calibration
SensorLive sensorLive();
