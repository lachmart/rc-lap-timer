#include "zroundproto.h"
#include <stdio.h>

void ZRoundProto::handle(char cmd, int64_t nowUs) {
  switch (cmd) {
    case 'C':                                         // connect: RiCino answers %A& 21 times, 100 ms apart
      conn = true;
      acksLeft = 21;
      nextAckUs = nowUs;
      break;
    case 'I':                                         // race start
      race = true;
      startUs = nowUs;
      nextTimeUs = startUs + 5000000;
      break;
    case 'F':                                         // race finish
      race = false;
      break;
  }
}

void ZRoundProto::feed(char c, int64_t nowUs) {
  if (c == '%') { msgLen = 0; return; }
  if (msgLen < 0) return;
  if (c == '&') { if (msgLen > 0) handle(msg[0], nowUs); msgLen = -1; return; }
  if (msgLen < (int)sizeof msg) msg[msgLen++] = c;
  else msgLen = -1;
}

void ZRoundProto::poll(int64_t nowUs, std::string& out) {
  if (acksLeft > 0 && nowUs >= nextAckUs) {
    out += "%A&";
    acksLeft--;
    nextAckUs += 100000;
  }
  if (race && nowUs >= nextTimeUs) {
    char buf[24];
    snprintf(buf, sizeof buf, "%%T%lX&\r\n", (unsigned long)((nextTimeUs - startUs) / 1000));
    out += buf;
    nextTimeUs += 5000000;
  }
}

void ZRoundProto::passing(uint16_t id, int64_t tUs, std::string& out) const {
  if (!race || id == 0 || tUs < startUs) return;
  char buf[24];
  snprintf(buf, sizeof buf, "%%L%X,%lX&", id, (unsigned long)((tUs - startUs) / 1000));
  out += buf;
}
