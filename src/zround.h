// USB serial port: debug log (115200 baud), or a ZRound lap counting decoder using RiCino's protocol
// (19200 baud). ZRound then runs the race itself; the app keeps working alongside.
//   ZRound → timer:  %C& connect, %I& race start, %F& race finish
//   timer → ZRound:  %A& connected (repeated), %T<ms hex>& every 5 s, %L<id hex>,<ms hex>& passing
// Times are ms since %I&, ids are the transponder ids. Only transponder cars are reported.
#pragma once
#include <stdint.h>

void zroundBegin();                         // opens the port in the mode from settings.zround
void zroundApply();                         // settings.zround changed
void zroundLoop();
void zroundPassing(uint16_t id, int64_t tUs);
bool zroundConnected();                     // ZRound sent %C& since the mode was switched on
bool zroundRacing();
