// Host unit tests for src/beam.cpp: pio test -e native
#include <unity.h>
#include "beam.h"

static constexpr int DELTA = 800;
static Beam beam;
static uint32_t t;                     // ms, one sample per ms like the sensor task

static int feed(int raw, int samples) {   // returns how many breaks were detected
  int breaks = 0;
  for (int i = 0; i < samples; i++) breaks += beam.update(raw, DELTA, t++);
  return breaks;
}

// A pass with a slow LDR: ramp down, dark, ramp back up.
static int pass(int idle, int dark) {
  int breaks = 0;
  for (int i = 0; i <= 20; i++) breaks += feed(idle + (dark - idle) * i / 20, 1);
  breaks += feed(dark, 30);
  for (int i = 20; i >= 0; i--) breaks += feed(idle + (dark - idle) * i / 20, 1);
  return breaks;
}

void setUp() {
  t = 1000;
  beam.calibrate(3000);
}
void tearDown() {}

void test_break_detected_once_per_pass() {
  TEST_ASSERT_EQUAL(0, feed(3000, 500));
  TEST_ASSERT_EQUAL(1, pass(3000, 500));
  TEST_ASSERT_FALSE(beam.broken());
  TEST_ASSERT_EQUAL(1, pass(3000, 500));
}

void test_either_polarity() {
  beam.calibrate(800);
  TEST_ASSERT_EQUAL(1, pass(800, 3500));              // LDR wired the other way: signal goes up
}

void test_hysteresis_no_double_trigger_on_wobble() {
  TEST_ASSERT_EQUAL(1, feed(3000 - 900, 5));          // broken
  TEST_ASSERT_EQUAL(0, feed(3000 - 700, 5));          // below delta but above delta/2: still broken
  TEST_ASSERT_TRUE(beam.broken());
  TEST_ASSERT_EQUAL(0, feed(3000 - 900, 5));          // no second trigger
  feed(3000 - 300, 5);
  TEST_ASSERT_FALSE(beam.broken());
}

void test_small_deviation_does_not_trigger() {
  TEST_ASSERT_EQUAL(0, feed(3000 - 700, 200));
  TEST_ASSERT_FALSE(beam.broken());
}

void test_idle_follows_slow_ambient_change() {
  for (int i = 0; i < 20000; i++) feed(3000 - i / 20, 1);   // −1000 over 20 s, slower than tracking
  TEST_ASSERT_FALSE(beam.broken());
  TEST_ASSERT_INT_WITHIN(100, 2000, beam.base());
}

void test_peak_is_depth_of_last_break() {
  pass(3000, 500);
  TEST_ASSERT_INT_WITHIN(20, 2500, beam.peak());
  pass(3000, 1800);
  TEST_ASSERT_INT_WITHIN(20, 1200, beam.peak());
}

void test_noise_measures_wander_while_clear() {
  for (int i = 0; i < 2000; i++) feed(3000 + ((i % 2) ? 60 : -60), 1);
  TEST_ASSERT_INT_WITHIN(15, 60, beam.noise());
}

void test_noise_ignores_ramps_around_a_pass() {
  feed(3000, 200);
  pass(3000, 500);
  feed(3000, 200);
  TEST_ASSERT_LESS_THAN(30, beam.noise());
}

void test_noise_decays() {
  for (int i = 0; i < 500; i++) feed(3000 + ((i % 2) ? 200 : -200), 1);
  int before = beam.noise();
  feed(3000, 3000);
  TEST_ASSERT_LESS_THAN(before / 4, beam.noise());
}

void test_calibrate_resets_measurements() {
  pass(3000, 500);
  beam.calibrate(2500);
  TEST_ASSERT_EQUAL(0, beam.peak());
  TEST_ASSERT_EQUAL(0, beam.noise());
  TEST_ASSERT_EQUAL(2500, beam.base());
}

void test_suggest_delta() {
  TEST_ASSERT_EQUAL(0, beamSuggestDelta(40, 0));      // no pass yet
  int s = beamSuggestDelta(40, 2500);
  TEST_ASSERT_TRUE(s / 2 > 40 + 25);                  // clear threshold above noise
  TEST_ASSERT_TRUE(s < 2500 * 7 / 10 + 1);            // trigger inside the break depth
  TEST_ASSERT_EQUAL(0, s % 50);
  TEST_ASSERT_EQUAL(-1, beamSuggestDelta(400, 900));  // too weak to separate
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_break_detected_once_per_pass);
  RUN_TEST(test_either_polarity);
  RUN_TEST(test_hysteresis_no_double_trigger_on_wobble);
  RUN_TEST(test_small_deviation_does_not_trigger);
  RUN_TEST(test_idle_follows_slow_ambient_change);
  RUN_TEST(test_peak_is_depth_of_last_break);
  RUN_TEST(test_noise_measures_wander_while_clear);
  RUN_TEST(test_noise_ignores_ramps_around_a_pass);
  RUN_TEST(test_noise_decays);
  RUN_TEST(test_calibrate_resets_measurements);
  RUN_TEST(test_suggest_delta);
  return UNITY_END();
}
