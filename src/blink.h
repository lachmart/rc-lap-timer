// "Blink" transponders: the cheapest possible car id, no microcontroller. An NE556 (dual 555)
// makes 38 kHz bursts for the IR receiver and switches them on and off at a car-specific rate,
// e.g. 320, 390, 465 Hz. The rate is the id. See tools/transponder/README.md.
//
// No hardware dependencies, unit-tested on the host.
#pragma once
#include <stdint.h>

constexpr uint16_t BLINK_HZ_MIN = 250;
constexpr uint16_t BLINK_HZ_MAX = 1500;     // all blink ids are below the RiCino range (0x1000+)

// Measures the blink rate from the IR receiver's edges.
class BlinkDecoder {
public:
  // mark = carrier starts (receiver output low). Returns true with the rate in Hz once
  // WINDOW periods in a row agree, then at most every EMIT_EVERY while the car stays in range.
  bool edge(bool mark, int64_t tUs, uint16_t& hz);
  void reset() { *this = BlinkDecoder(); }

private:
  static constexpr int     WINDOW     = 8;
  static constexpr int64_t EMIT_EVERY = 50000;    // µs
  static constexpr int64_t LONG_MARK  = 3000;     // µs: an NEC header (9 ms), not a blink
  static constexpr int64_t NEC_FRAME  = 80000;    // µs to ignore after an NEC header

  int64_t  markStartUs_ = 0;
  int64_t  prevMarkUs_  = 0;
  int64_t  ignoreUntil_ = 0;
  int64_t  lastEmitUs_  = -1000000000;
  uint32_t periods_[WINDOW] = {};
  int      n_ = 0;
};

// Maps measured rates to stable ids: a reading within ±TOLERANCE of a known rate gets that rate
// as its id, an unknown rate is learned. Transponders only need to be ~15 % apart, exact
// component values don't matter.
class BlinkIds {
public:
  static constexpr int MAX = 16;
  static constexpr int TOLERANCE_PCT = 6;

  void     add(uint16_t hz);           // known transponder (from the saved car list)
  uint16_t identify(uint16_t hz);      // 0 if out of range or the table is full

private:
  uint16_t known_[MAX] = {};
  int      n_ = 0;
};
