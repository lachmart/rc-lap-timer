#include "beam.h"
#include <stdlib.h>

void Beam::calibrate(int idle) {
  base_ = idle;
  broken_ = false;
  noise_ = 0;
  peak_ = curPeak_ = 0;
  histLen_ = histPos_ = 0;
  holdUntil_ = 0;
}

bool Beam::update(int raw, int delta, uint32_t nowMs) {
  int dev = abs(raw - (int)base_);

  if (broken_) {
    if (dev > curPeak_) curPeak_ = dev;
    if (dev < delta / 2) {                            // hysteresis, beam is clear again
      broken_ = false;
      peak_ = curPeak_;
      holdUntil_ = nowMs + HOLD_OFF;                  // the LDR is still recovering
    }
    return false;
  }

  if (dev > delta) {
    broken_ = true;
    curPeak_ = dev;
    histLen_ = 0;                                     // the ramp into this break isn't noise
    return true;
  }

  base_ += (raw - base_) * TRACK;

  noise_ *= NOISE_DECAY;
  if ((int32_t)(nowMs - holdUntil_) >= 0) {
    if (histLen_ == DELAY) {                          // oldest sample is old enough to be trusted
      int old = hist_[histPos_];
      if (old > noise_) noise_ = old;
    } else {
      histLen_++;
    }
    hist_[histPos_] = (uint16_t)dev;
    histPos_ = (histPos_ + 1) % DELAY;
  }
  return false;
}

int beamSuggestDelta(int noise, int peak) {
  if (peak <= 0) return 0;
  int lo = 2 * noise + 50;                            // clear threshold (delta / 2) above the noise
  int hi = peak * 7 / 10;                             // trigger well inside the break depth
  if (lo > hi) return -1;
  int s = (lo + hi) / 2;
  s = (s + 25) / 50 * 50;
  if (s < 50) s = 50;
  if (s > 4000) s = 4000;
  return s;
}
