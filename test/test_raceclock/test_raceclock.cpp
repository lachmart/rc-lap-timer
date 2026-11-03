// Host unit tests for src/raceclock.cpp: pio test -e native
#include <unity.h>
#include "raceclock.h"

static RaceClock r;

static int64_t ms(int64_t v) { return v * 1000; }

void setUp() { r = RaceClock(); }
void tearDown() {}

void test_training_counts_everything() {
  TEST_ASSERT_TRUE(r.info().state == RaceState::Off);
  TEST_ASSERT_FALSE(r.update(ms(1000)));
  TEST_ASSERT_TRUE(r.counts(0));
  TEST_ASSERT_TRUE(r.counts(ms(5000)));
}

void test_start_counts_down() {
  r.start(ms(10000), 4000, 10, 0);
  RaceInfo i = r.info();
  TEST_ASSERT_TRUE(i.state == RaceState::Countdown);
  TEST_ASSERT_EQUAL_INT64(ms(14000), i.goUs);
  TEST_ASSERT_EQUAL_INT(10, i.laps);
  TEST_ASSERT_EQUAL_INT(0, i.minutes);
}

void test_passings_during_countdown_are_ignored() {
  r.start(ms(10000), 4000, 0, 5);
  TEST_ASSERT_FALSE(r.counts(ms(11000)));
  TEST_ASSERT_FALSE(r.counts(ms(20000)));   // even one stamped after "go", until update() sees "go"
}

void test_go_fires_once() {
  r.start(ms(10000), 4000, 10, 0);
  TEST_ASSERT_FALSE(r.update(ms(13999)));
  TEST_ASSERT_TRUE(r.info().state == RaceState::Countdown);
  TEST_ASSERT_TRUE(r.update(ms(14000)));
  TEST_ASSERT_TRUE(r.info().state == RaceState::Running);
  TEST_ASSERT_FALSE(r.update(ms(15000)));
}

void test_running_ignores_passings_before_go() {
  r.start(ms(10000), 4000, 10, 0);
  r.update(ms(14500));
  TEST_ASSERT_FALSE(r.counts(ms(13990)));   // queued before "go", handled after it
  TEST_ASSERT_TRUE(r.counts(ms(14000)));
  TEST_ASSERT_TRUE(r.counts(ms(14600)));
}

void test_stop_back_to_training() {
  r.start(ms(10000), 4000, 10, 0);
  r.update(ms(14000));
  r.stop();
  TEST_ASSERT_TRUE(r.info().state == RaceState::Off);
  TEST_ASSERT_TRUE(r.counts(ms(1000)));
  TEST_ASSERT_FALSE(r.update(ms(20000)));
}

void test_stop_during_countdown_never_goes() {
  r.start(ms(10000), 4000, 10, 0);
  r.stop();
  TEST_ASSERT_FALSE(r.update(ms(20000)));
  TEST_ASSERT_TRUE(r.info().state == RaceState::Off);
}

void test_restart_replaces_race() {
  r.start(ms(10000), 4000, 10, 0);
  r.update(ms(14000));
  r.start(ms(30000), 4000, 0, 3);
  TEST_ASSERT_TRUE(r.info().state == RaceState::Countdown);
  TEST_ASSERT_EQUAL_INT(3, r.info().minutes);
  TEST_ASSERT_FALSE(r.counts(ms(35000)));
  TEST_ASSERT_TRUE(r.update(ms(34000)));
}

void test_heats_in_order() {
  const uint8_t order[] = { 3, 1, 2 };
  r.start(ms(10000), 4000, 5, 0, order, 3);
  TEST_ASSERT_EQUAL_INT(3, r.info().nHeats);
  TEST_ASSERT_EQUAL_UINT8(3, r.heatDriver());          // known before "go", so the car gets driver 3 at once
  TEST_ASSERT_FALSE(r.heatLaps(5));                    // not racing yet
  r.update(ms(14000));
  TEST_ASSERT_FALSE(r.heatLaps(4));
  TEST_ASSERT_TRUE(r.heatLaps(5));
  TEST_ASSERT_EQUAL_UINT8(1, r.heatDriver());
  TEST_ASSERT_FALSE(r.heatLaps(0));
  TEST_ASSERT_TRUE(r.heatLaps(5));
  TEST_ASSERT_EQUAL_UINT8(2, r.heatDriver());
  TEST_ASSERT_TRUE(r.heatLaps(6));
  TEST_ASSERT_EQUAL_UINT8(0, r.heatDriver());          // all done
  TEST_ASSERT_EQUAL_INT(3, r.info().heat);
  TEST_ASSERT_FALSE(r.heatLaps(99));
}

void test_no_heats_without_laps_or_order() {
  const uint8_t order[] = { 1, 2 };
  r.start(ms(0), 4000, 0, 5, order, 2);                // time race: no heats
  TEST_ASSERT_EQUAL_INT(0, r.info().nHeats);
  TEST_ASSERT_EQUAL_UINT8(0, r.heatDriver());
  r.start(ms(0), 4000, 10, 0);
  TEST_ASSERT_EQUAL_UINT8(0, r.heatDriver());
  r.update(ms(4000));
  TEST_ASSERT_FALSE(r.heatLaps(10));
}

void test_heats_skip_and_limits() {
  const uint8_t order[] = { 0, 4, 5, 6, 7, 8, 9, 10, 11, 12 };   // 0 is not a driver; at most MAX_DRIVERS heats
  r.start(ms(0), 4000, 3, 0, order, 10);
  TEST_ASSERT_EQUAL_INT(MAX_DRIVERS, r.info().nHeats);
  TEST_ASSERT_EQUAL_UINT8(4, r.heatDriver());
  r.skipHeat();                                         // works during the countdown too
  TEST_ASSERT_EQUAL_UINT8(5, r.heatDriver());
}

void test_restart_clears_heats() {
  const uint8_t order[] = { 1, 2 };
  r.start(ms(0), 4000, 3, 0, order, 2);
  r.update(ms(4000));
  r.heatLaps(3);
  r.start(ms(10000), 4000, 3, 0, order, 2);
  TEST_ASSERT_EQUAL_INT(0, r.info().heat);
  TEST_ASSERT_EQUAL_UINT8(1, r.heatDriver());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_training_counts_everything);
  RUN_TEST(test_start_counts_down);
  RUN_TEST(test_passings_during_countdown_are_ignored);
  RUN_TEST(test_go_fires_once);
  RUN_TEST(test_running_ignores_passings_before_go);
  RUN_TEST(test_stop_back_to_training);
  RUN_TEST(test_stop_during_countdown_never_goes);
  RUN_TEST(test_restart_replaces_race);
  RUN_TEST(test_heats_in_order);
  RUN_TEST(test_no_heats_without_laps_or_order);
  RUN_TEST(test_heats_skip_and_limits);
  RUN_TEST(test_restart_clears_heats);
  return UNITY_END();
}
