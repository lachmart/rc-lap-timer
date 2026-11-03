// Debug log on the USB serial port. Silent while the port speaks the ZRound protocol (zround.h).
#pragma once
#include <Arduino.h>

extern volatile bool logEnabled;

#define LOGF(...) do { if (logEnabled) Serial.printf(__VA_ARGS__); } while (0)
