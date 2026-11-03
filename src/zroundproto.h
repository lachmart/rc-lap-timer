// RiCino decoder side of the ZRound serial protocol (see zround.h). Bytes from ZRound go in,
// text to send comes out. No hardware dependencies (time passed in), unit-tested (test/test_zround).
#pragma once
#include <stdint.h>
#include <string>

class ZRoundProto {
 public:
  void feed(char c, int64_t nowUs);                            // a byte from ZRound
  void poll(int64_t nowUs, std::string& out);                  // appends an ack / time sync when due
  void passing(uint16_t id, int64_t tUs, std::string& out) const;
  bool connected() const { return conn; }
  bool racing() const    { return race; }

 private:
  void handle(char cmd, int64_t nowUs);

  bool    conn      = false;
  bool    race      = false;
  int64_t startUs   = 0;
  int64_t nextTimeUs = 0;
  int     acksLeft  = 0;
  int64_t nextAckUs = 0;
  char    msg[8];
  int     msgLen    = -1;                                      // -1 = waiting for '%'
};
