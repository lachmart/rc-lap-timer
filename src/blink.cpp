#include "blink.h"

bool BlinkDecoder::edge(bool mark, int64_t tUs, uint16_t& hz) {
  if (!mark) {                                        // burst ended
    if (tUs - markStartUs_ > LONG_MARK) {             // NEC header: its zero bits would look like a blink
      ignoreUntil_ = tUs + NEC_FRAME;
      n_ = 0;
    }
    return false;
  }

  markStartUs_ = tUs;
  int64_t period = tUs - prevMarkUs_;
  prevMarkUs_ = tUs;
  if (tUs < ignoreUntil_ || period < 1000000 / BLINK_HZ_MAX || period > 1000000 / BLINK_HZ_MIN) {
    n_ = 0;
    return false;
  }

  if (n_ < WINDOW) periods_[n_++] = (uint32_t)period;
  else {
    for (int i = 1; i < WINDOW; i++) periods_[i - 1] = periods_[i];
    periods_[WINDOW - 1] = (uint32_t)period;
  }
  if (n_ < WINDOW) return false;

  uint32_t sum = 0;
  for (int i = 0; i < WINDOW; i++) sum += periods_[i];
  uint32_t mean = sum / WINDOW;
  for (int i = 0; i < WINDOW; i++) {                  // all within ±5 %: a steady blink
    uint32_t d = periods_[i] > mean ? periods_[i] - mean : mean - periods_[i];
    if (d * 20 > mean) return false;
  }
  if (tUs - lastEmitUs_ < EMIT_EVERY) return false;
  lastEmitUs_ = tUs;
  hz = (uint16_t)((1000000 + mean / 2) / mean);
  return true;
}

void BlinkIds::add(uint16_t hz) {
  if (identify(hz) == 0 && n_ < MAX) known_[n_++] = hz;
}

uint16_t BlinkIds::identify(uint16_t hz) {
  if (hz < BLINK_HZ_MIN || hz > BLINK_HZ_MAX) return 0;
  int best = -1;
  uint32_t bestDiff = 0;
  for (int i = 0; i < n_; i++) {
    uint32_t d = known_[i] > hz ? known_[i] - hz : hz - known_[i];
    if (d * 100 <= (uint32_t)known_[i] * TOLERANCE_PCT && (best < 0 || d < bestDiff)) { best = i; bestDiff = d; }
  }
  if (best >= 0) return known_[best];
  if (n_ == MAX) return 0;
  known_[n_++] = hz;
  return hz;
}
