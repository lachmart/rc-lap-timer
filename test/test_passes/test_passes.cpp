// Host unit tests for src/passes.cpp: pio test -e native
#include <unity.h>
#include "passes.h"

static PassMatcher m;
static Passing out[16];

static int64_t ms(double v) { return (int64_t)(v * 1000); }

// A car passing the gate: its transponder is seen every 70 ms from `fromMs` to `toMs`.
static void sightings(uint16_t id, double fromMs, double toMs) {
  for (double t = fromMs; t <= toMs; t += 70) m.ir(id, ms(t));
}

void setUp() { m = PassMatcher(); m.setMulti(true); }
void tearDown() {}

void test_single_mode_every_beam_is_a_passing() {
  m.setMulti(false);
  m.beam(ms(1000));
  m.ir(0x1B39, ms(1000));                             // IR ignored in single-car mode
  TEST_ASSERT_EQUAL(1, m.poll(ms(1001), out, 16));
  TEST_ASSERT_EQUAL(0, out[0].id);
  TEST_ASSERT_EQUAL(ms(1000), out[0].tUs);
  TEST_ASSERT_TRUE(out[0].precise);
}

void test_beam_inside_cluster_gives_precise_time() {
  sightings(0x1B39, 900, 1200);
  m.beam(ms(1043));
  TEST_ASSERT_EQUAL(0, m.poll(ms(1300), out, 16));    // car still near the gate
  TEST_ASSERT_EQUAL(1, m.poll(ms(1500), out, 16));
  TEST_ASSERT_EQUAL_HEX16(0x1B39, out[0].id);
  TEST_ASSERT_EQUAL(ms(1043), out[0].tUs);
  TEST_ASSERT_TRUE(out[0].precise);
  TEST_ASSERT_EQUAL(0, m.unidentified());
}

void test_ir_only_passing_is_counted_imprecise() {
  sightings(0x2000, 1000, 1280);
  TEST_ASSERT_EQUAL(1, m.poll(ms(2000), out, 16));
  TEST_ASSERT_EQUAL_HEX16(0x2000, out[0].id);
  TEST_ASSERT_EQUAL(ms(1140), out[0].tUs);            // middle of the sightings
  TEST_ASSERT_FALSE(out[0].precise);
}

void test_beam_without_transponder_is_unidentified() {
  m.beam(ms(1000));
  TEST_ASSERT_EQUAL(0, m.poll(ms(1100), out, 16));
  TEST_ASSERT_EQUAL(0, m.unidentified());             // a car could still show up
  TEST_ASSERT_EQUAL(0, m.poll(ms(1500), out, 16));
  TEST_ASSERT_EQUAL(1, m.unidentified());
}

void test_beam_just_before_ir_still_matches() {
  m.beam(ms(1000));                                   // receiver sees the car a bit later
  sightings(0x3000, 1150, 1400);
  TEST_ASSERT_EQUAL(1, m.poll(ms(2000), out, 16));
  TEST_ASSERT_EQUAL(ms(1000), out[0].tUs);
  TEST_ASSERT_TRUE(out[0].precise);
}

void test_far_beam_is_not_taken() {
  m.beam(ms(500));
  sightings(0x3000, 1000, 1200);
  m.poll(ms(2000), out, 16);
  TEST_ASSERT_FALSE(out[0].precise);
  TEST_ASSERT_EQUAL(1, m.unidentified());
}

void test_two_cars_close_together_each_get_their_beam() {
  sightings(0x1111, 1000, 1210);                      // car A
  sightings(0x2222, 1180, 1390);                      // car B right behind
  m.beam(ms(1100));
  m.beam(ms(1290));
  int n = m.poll(ms(2000), out, 16);
  TEST_ASSERT_EQUAL(2, n);
  TEST_ASSERT_EQUAL_HEX16(0x1111, out[0].id);
  TEST_ASSERT_EQUAL(ms(1100), out[0].tUs);
  TEST_ASSERT_EQUAL_HEX16(0x2222, out[1].id);
  TEST_ASSERT_EQUAL(ms(1290), out[1].tUs);
  TEST_ASSERT_TRUE(out[0].precise && out[1].precise);
}

void test_false_trigger_next_to_a_pass_goes_unidentified() {
  sightings(0x1111, 1000, 1200);
  m.beam(ms(1100));
  m.beam(ms(1350));                                   // hand in the beam right after
  m.poll(ms(2000), out, 16);
  TEST_ASSERT_EQUAL(ms(1100), out[0].tUs);
  TEST_ASSERT_EQUAL(1, m.unidentified());
}

void test_consecutive_laps_are_separate_passings() {
  sightings(0x1111, 1000, 1200);
  m.beam(ms(1100));
  TEST_ASSERT_EQUAL(1, m.poll(ms(1500), out, 16));
  sightings(0x1111, 9000, 9200);
  m.beam(ms(9100));
  TEST_ASSERT_EQUAL(1, m.poll(ms(9500), out, 16));
  TEST_ASSERT_EQUAL(ms(9100), out[0].tUs);
}

void test_parked_car_does_not_flood() {
  int total = 0;
  for (double t = 0; t <= 6000; t += 70) {             // sits under the receiver for 5 s
    if (t <= 5000) m.ir(0x1111, ms(t));
    total += m.poll(ms(t), out, 16);
  }
  TEST_ASSERT_TRUE(total >= 2 && total <= 4);          // one per MAX_CLUSTER, not one per sighting
}

void test_switching_mode_clears_pending() {
  sightings(0x1111, 1000, 1200);
  m.beam(ms(1100));
  m.setMulti(false);
  TEST_ASSERT_EQUAL(0, m.poll(ms(2000), out, 16));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_single_mode_every_beam_is_a_passing);
  RUN_TEST(test_beam_inside_cluster_gives_precise_time);
  RUN_TEST(test_ir_only_passing_is_counted_imprecise);
  RUN_TEST(test_beam_without_transponder_is_unidentified);
  RUN_TEST(test_beam_just_before_ir_still_matches);
  RUN_TEST(test_far_beam_is_not_taken);
  RUN_TEST(test_two_cars_close_together_each_get_their_beam);
  RUN_TEST(test_false_trigger_next_to_a_pass_goes_unidentified);
  RUN_TEST(test_consecutive_laps_are_separate_passings);
  RUN_TEST(test_parked_car_does_not_flood);
  RUN_TEST(test_switching_mode_clears_pending);
  return UNITY_END();
}
