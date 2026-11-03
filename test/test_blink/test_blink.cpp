// Host unit tests for src/blink.cpp: pio test -e native
#include <unity.h>
#include <stdlib.h>
#include "blink.h"
#include "nec.h"

static BlinkDecoder dec;
static int64_t t;

// NE556 transponder seen by the IR receiver for `ms`: bursts at `hz`, ~51 % duty, small jitter.
// Returns the last rate measured, 0 if none.
static uint16_t blink(double hz, int ms, int jitterUs = 15) {
  uint16_t got = 0, h;
  int64_t end = t + (int64_t)ms * 1000;
  double period = 1e6 / hz;
  while (t < end) {
    int j = jitterUs ? (rand() % (2 * jitterUs + 1)) - jitterUs : 0;
    if (dec.edge(true, t, h)) got = h;
    t += (int64_t)(period * 0.51) + j;
    if (dec.edge(false, t, h)) got = h;
    t += (int64_t)(period * 0.49);
  }
  return got;
}

void setUp() { dec.reset(); t = 1000000; srand(1); }
void tearDown() {}

void test_measures_rate() {
  TEST_ASSERT_INT_WITHIN(3, 465, blink(465, 100));
  TEST_ASSERT_INT_WITHIN(3, 320, blink(320, 100));
  TEST_ASSERT_INT_WITHIN(8, 1180, blink(1180, 100));
}

void test_too_short_glimpse_gives_nothing() {
  TEST_ASSERT_EQUAL(0, blink(465, 12));               // fewer than 8 periods
}

void test_rate_out_of_range_ignored() {
  TEST_ASSERT_EQUAL(0, blink(150, 200));
  TEST_ASSERT_EQUAL(0, blink(3000, 200));
}

void test_emits_repeatedly_but_not_every_burst() {
  int count = 0;
  uint16_t h;
  double period = 1e6 / 500;
  for (int i = 0; i < 250; i++) {                      // 0.5 s in range
    if (dec.edge(true, t, h)) count++;
    t += (int64_t)(period / 2);
    dec.edge(false, t, h);
    t += (int64_t)(period / 2);
  }
  TEST_ASSERT_TRUE(count >= 8 && count <= 11);         // every 50 ms
}

void test_nec_frame_is_not_a_blink() {
  uint16_t d[68];
  uint16_t h;
  bool any = false;
  for (int frame = 0; frame < 5; frame++) {            // RiCino 0x1000: 16 leading zero bits
    int n = necEncode(0x1000, d);
    for (int i = 0; i < n; i++) { any |= dec.edge(i % 2 == 0, t, h); t += d[i]; }
    any |= dec.edge(true, t, h);
    t += 10000;
  }
  TEST_ASSERT_FALSE(any);
}

void test_ids_tolerate_drift_and_learn() {
  BlinkIds ids;
  TEST_ASSERT_EQUAL(465, ids.identify(465));           // learned
  TEST_ASSERT_EQUAL(465, ids.identify(480));           // +3 %: same car
  TEST_ASSERT_EQUAL(465, ids.identify(447));
  TEST_ASSERT_EQUAL(390, ids.identify(390));           // −16 %: another car
  TEST_ASSERT_EQUAL(390, ids.identify(400));
  TEST_ASSERT_EQUAL(0, ids.identify(100));
}

void test_ids_seeded_from_saved_cars() {
  BlinkIds ids;
  ids.add(576);
  TEST_ASSERT_EQUAL(576, ids.identify(590));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_measures_rate);
  RUN_TEST(test_too_short_glimpse_gives_nothing);
  RUN_TEST(test_rate_out_of_range_ignored);
  RUN_TEST(test_emits_repeatedly_but_not_every_burst);
  RUN_TEST(test_nec_frame_is_not_a_blink);
  RUN_TEST(test_ids_tolerate_drift_and_learn);
  RUN_TEST(test_ids_seeded_from_saved_cars);
  return UNITY_END();
}
