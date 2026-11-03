// Beam break detection from raw ADC samples. No hardware dependencies, unit-tested on the host.
//
// Detection = deviation from the idle value (|raw - base| > delta), so wiring polarity doesn't matter.
// Hysteresis: the beam counts as clear again below delta / 2. While clear, the idle value slowly
// follows ambient light.
//
// For tuning it also measures:
//  - noise: how far the signal wanders while the beam is clear (decaying peak hold, ~1 s).
//    The LDR is slow, so samples just before a break and just after it are left out,
//    otherwise every pass would look like noise.
//  - peak:  the deepest deviation during the last completed break.
#pragma once
#include <stdint.h>

class Beam {
public:
  void calibrate(int idle);                            // idle = average with the beam on the sensor
  bool update(int raw, int delta, uint32_t nowMs);     // true on the sample where the beam got broken

  int  base()   const { return (int)base_; }
  bool broken() const { return broken_; }
  int  noise()  const { return (int)noise_; }
  int  peak()   const { return peak_; }

private:
  static constexpr int      DELAY      = 64;    // samples (~64 ms) before a sample counts as noise
  static constexpr uint32_t HOLD_OFF   = 300;   // ms after a break before noise is measured again
  static constexpr float    TRACK      = 0.0005f;  // idle follows ambient light (~2 s at 1 kHz)
  static constexpr float    NOISE_DECAY = 0.9993f; // noise peak hold half-life ~1 s at 1 kHz

  float    base_     = 0;
  bool     broken_   = false;
  float    noise_    = 0;
  int      peak_     = 0;
  int      curPeak_  = 0;
  uint16_t hist_[DELAY] = {};
  int      histLen_  = 0;
  int      histPos_  = 0;
  uint32_t holdUntil_ = 0;
};

// Sensitivity (delta) that sits safely between noise and the break depth, rounded to 50.
// 0 = no pass measured yet, -1 = the signal is too weak to separate (aim the laser, shield the LDR).
int beamSuggestDelta(int noise, int peak);
