// Fixed firmware settings
#pragma once
#include <stdint.h>

constexpr int      SENSOR_PIN  = 32;              // ADC1 only (GPIO 32–39), ADC2 can't be used with Wi-Fi on
constexpr const char* AP_SSID  = "RC_Lap_Timer";
constexpr const char* AP_PASS  = "";              // empty = open network, otherwise at least 8 characters
constexpr const char* HOSTNAME = "laptimer";      // http://laptimer.local
constexpr int      MAX_LAPS    = 200;             // how many recent laps are kept in memory
constexpr uint32_t DEF_MIN_LAP = 2000;            // ms, shorter passes are ignored
constexpr int      DEF_DELTA   = 800;             // how far the ADC value must deviate from idle
constexpr uint32_t STA_TIMEOUT = 10000;           // ms, how long to try the home Wi-Fi at startup
constexpr uint32_t LIVE_EVERY  = 200;             // ms, live sensor data to the browser
