#include "nec.h"

static constexpr int HDR_MARK  = 9000;
static constexpr int HDR_SPACE = 4500;
static constexpr int BIT_MARK  = 560;
static constexpr int ONE_SPACE = 1690;
static constexpr int ZERO_SPACE = 560;

// IR receivers stretch marks and shorten spaces by ~100–200 µs, so allow ±30 %.
static bool near(int64_t d, int nominal) {
  return d > nominal * 7 / 10 && d < nominal * 13 / 10;
}

bool NecDecoder::edge(bool mark, int64_t tUs, uint32_t& code) {
  int64_t d = tUs - lastUs_;              // duration of what just ended
  lastUs_ = tUs;

  switch (state_) {
    case Idle:
      if (mark) state_ = HeaderMark;
      return false;

    case HeaderMark:                      // header mark ended
      state_ = (!mark && near(d, HDR_MARK)) ? HeaderSpace : (mark ? HeaderMark : Idle);
      return false;

    case HeaderSpace:                     // header space ended (a 2.25 ms repeat code isn't an id)
      if (mark && near(d, HDR_SPACE)) { state_ = BitMark; bits_ = 0; count_ = 0; }
      else state_ = mark ? HeaderMark : Idle;
      return false;

    case BitMark:                         // a bit mark (or the final mark) ended
      if (mark || !near(d, BIT_MARK)) { state_ = mark ? HeaderMark : Idle; return false; }
      if (count_ == 32) {                 // final mark: frame complete
        state_ = Idle;
        code = bits_;
        return true;
      }
      state_ = BitSpace;
      return false;

    case BitSpace:                        // a bit space ended, its length is the bit value
      if (!mark) { state_ = Idle; return false; }
      if (near(d, ONE_SPACE))       bits_ = (bits_ << 1) | 1;
      else if (near(d, ZERO_SPACE)) bits_ = bits_ << 1;
      else { state_ = HeaderMark; return false; }   // garbage: this mark may start a new header
      count_++;
      state_ = BitMark;
      return false;
  }
  return false;
}

int necEncode(uint32_t code, uint16_t* out) {
  int n = 0;
  out[n++] = HDR_MARK;
  out[n++] = HDR_SPACE;
  for (int i = 31; i >= 0; i--) {
    out[n++] = BIT_MARK;
    out[n++] = (code >> i) & 1 ? ONE_SPACE : ZERO_SPACE;
  }
  out[n++] = BIT_MARK;
  return n;
}

uint16_t necTransponderId(uint32_t code) {
  if (code >> 16) return 0;               // RiCino ids use only the low 16 bits
  uint16_t id = (uint16_t)code;
  return id >= NEC_ID_MIN ? id : 0;
}
