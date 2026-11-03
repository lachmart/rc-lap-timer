// Laser beam gate: samples the LDR in a separate FreeRTOS task, detection itself is in beam.h
#pragma once

struct SensorLive {
  int  raw;      // latest ADC value
  int  base;     // idle value (beam on the sensor)
  bool broken;   // the beam is currently interrupted
  int  noise;    // how far the signal wanders with a clear beam
  int  peak;     // depth of the last beam break
  int  suggest;  // suggested sensitivity, 0 = no pass yet, -1 = signal too weak
};

void       sensorBegin();        // sets up the ADC and starts the task (calibrates right after start)
void       sensorCalibrate();    // request a new idle calibration
SensorLive sensorLive();
