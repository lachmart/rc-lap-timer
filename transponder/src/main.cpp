// RC Lap Timer transponder: ATtiny202 + IR LED, sends a RiCino-compatible id (NEC code, 38 kHz).
// See transponder/README.md for the circuit and how to program it.
//
// Frame (same as RiCino's IRremote sendNEC(id, 32)): 9 ms carrier + 4.5 ms pause, 32 bits MSB
// first (560 µs carrier, then 560 µs pause = 0 or 1690 µs pause = 1), 560 µs carrier at the end.
// The id sits in the low 16 bits. After each frame a random 5–20 ms pause, so two transponders
// near the gate don't keep colliding.
//
// The carrier comes from TCA0 (PWM on PA3, the LED transistor's base); between marks the
// compare output is switched off and PA3 is low. The core may use TCA0 for millis()/PWM, so it is
// reset and taken over completely; all waiting is busy-waiting, no millis()/delay().
#include <Arduino.h>

#ifndef TRANSPONDER_ID
#define TRANSPONDER_ID 0x1000            // set per car: PLATFORMIO_BUILD_FLAGS="-DTRANSPONDER_ID=0x1001"
#endif
static_assert(TRANSPONDER_ID >= 0x1000 && TRANSPONDER_ID <= 0xFFFF, "RiCino ids are 0x1000–0xFFFF");

static constexpr uint32_t CODE    = TRANSPONDER_ID;
static constexpr uint16_t CARRIER = 38000;

static inline void carrierOn()  { TCA0.SINGLE.CTRLB |= TCA_SINGLE_CMP0EN_bm; }
static inline void carrierOff() { TCA0.SINGLE.CTRLB &= ~TCA_SINGLE_CMP0EN_bm; PORTA.OUTCLR = PIN3_bm; }

static void mark(uint16_t us)  { carrierOn();  delayMicroseconds(us); }
static void space(uint16_t us) { carrierOff(); delayMicroseconds(us); }

static void sendFrame() {
  mark(9000);
  space(4500);
  for (int8_t i = 31; i >= 0; i--) {
    mark(560);
    space((CODE >> i) & 1 ? 1690 : 560);
  }
  mark(560);
  carrierOff();
}

static void waitMs(uint16_t ms) { while (ms--) delayMicroseconds(1000); }

// Tiny xorshift, seeded by the id so every transponder gets a different pause sequence.
static uint16_t rnd = TRANSPONDER_ID;
static uint16_t random16() {
  rnd ^= rnd << 7;
  rnd ^= rnd >> 9;
  rnd ^= rnd << 8;
  return rnd;
}

void setup() {
  PORTA.DIRSET = PIN3_bm;                // PA3 = TCA0 WO0
  PORTA.OUTCLR = PIN3_bm;

  // 38 kHz single-slope PWM, ~1/3 duty (typical for IR remotes, saves LED current)
  TCA0.SINGLE.CTRLA = 0;                               // stop, then reset whatever the core set up
  TCA0.SINGLE.CTRLESET = TCA_SINGLE_CMD_RESET_gc;
  TCA0.SINGLE.CTRLD = 0;                               // no split mode
  TCA0.SINGLE.INTCTRL = 0;                             // no millis() interrupt at 38 kHz
  TCA0.SINGLE.CTRLB = TCA_SINGLE_WGMODE_SINGLESLOPE_gc;
  TCA0.SINGLE.PER   = F_CPU / CARRIER - 1;
  TCA0.SINGLE.CMP0  = (F_CPU / CARRIER) / 3;
  TCA0.SINGLE.CTRLA = TCA_SINGLE_CLKSEL_DIV1_gc | TCA_SINGLE_ENABLE_bm;
}

void loop() {
  sendFrame();
  waitMs(5 + random16() % 16);           // 5–20 ms
}
