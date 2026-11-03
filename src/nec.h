// NEC infrared code, as sent by RiCino-compatible lap timing transponders:
// 38 kHz carrier, 9 ms mark + 4.5 ms space, 32 bits MSB first (0 = 560 µs mark + 560 µs space,
// 1 = 560 µs mark + 1690 µs space), final 560 µs mark. RiCino puts the transponder id
// (0x1000–0xFFFF) in the low 16 bits, the high 16 bits are zero.
//
// No hardware dependencies: the decoder is fed edge timestamps (unit-tested on the host), and the
// encoder produces the mark/space durations the transponder firmware plays back.
#pragma once
#include <stdint.h>

constexpr uint16_t NEC_ID_MIN = 0x1000;     // RiCino range
constexpr uint16_t NEC_ID_MAX = 0xFFFF;

class NecDecoder {
public:
  // Feed every edge of the IR receiver output. mark = true when the carrier starts (TSOP output
  // goes low), false when it stops. Returns true when a complete code was decoded into `code`.
  bool edge(bool mark, int64_t tUs, uint32_t& code);
  void reset() { state_ = Idle; }

private:
  enum State : uint8_t { Idle, HeaderMark, HeaderSpace, BitMark, BitSpace };
  State    state_ = Idle;
  int64_t  lastUs_ = 0;
  uint32_t bits_ = 0;
  int      count_ = 0;
};

// Mark/space durations in µs of one frame, starting with a mark. Returns how many were written
// (67 for 32 bits). `out` must hold at least 68 entries.
int necEncode(uint32_t code, uint16_t* out);

// Transponder id from a decoded code, 0 if it isn't a RiCino transponder code.
uint16_t necTransponderId(uint32_t code);
