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

void test_heats_in_order_manual_start() {
  const uint8_t order[] = { 3, 1, 2 };
  r.start(ms(10000), 4000, 5, 0, order, 3);              // prep 0: each next driver is started by hand
  TEST_ASSERT_EQUAL_INT(3, r.info().nHeats);
  TEST_ASSERT_EQUAL_UINT8(3, r.heatDriver());          // known before "go", so the car gets driver 3 at once
  TEST_ASSERT_FALSE(r.heatLaps(5, ms(11000)));         // not racing yet
  TEST_ASSERT_TRUE(r.update(ms(14000)));
  TEST_ASSERT_FALSE(r.heatLaps(4, ms(40000)));
  TEST_ASSERT_TRUE(r.heatLaps(5, ms(50000)));
  TEST_ASSERT_EQUAL_UINT8(1, r.heatDriver());
  TEST_ASSERT_TRUE(r.info().heatState == HeatState::Waiting);
  TEST_ASSERT_FALSE(r.counts(ms(52000)));               // driver 1 gets ready: the car's crossings don't count
  TEST_ASSERT_FALSE(r.update(ms(90000)));               // waits for the start
  TEST_ASSERT_FALSE(r.heatLaps(5, ms(90000)));
  TEST_ASSERT_TRUE(r.startHeat(ms(100000)));
  TEST_ASSERT_TRUE(r.info().heatState == HeatState::Countdown);
  TEST_ASSERT_EQUAL_INT64(ms(104000), r.info().heatGoUs);
  TEST_ASSERT_FALSE(r.update(ms(103999)));
  TEST_ASSERT_FALSE(r.counts(ms(103000)));
  TEST_ASSERT_TRUE(r.update(ms(104000)));               // driver 1's "go"
  TEST_ASSERT_TRUE(r.info().heatState == HeatState::Running);
  TEST_ASSERT_FALSE(r.counts(ms(103500)));              // queued from before their go
  TEST_ASSERT_TRUE(r.counts(ms(104100)));
  TEST_ASSERT_TRUE(r.heatLaps(5, ms(140000)));
  TEST_ASSERT_EQUAL_UINT8(2, r.heatDriver());
  r.startHeat(ms(150000));
  r.update(ms(154000));
  TEST_ASSERT_TRUE(r.heatLaps(6, ms(190000)));
  TEST_ASSERT_EQUAL_UINT8(0, r.heatDriver());          // all done
  TEST_ASSERT_EQUAL_INT(3, r.info().heat);
  TEST_ASSERT_FALSE(r.heatLaps(99, ms(200000)));
  TEST_ASSERT_FALSE(r.startHeat(ms(200000)));
}

void test_heats_prep_time() {
  const uint8_t order[] = { 1, 2 };
  r.start(ms(0), 4000, 3, 0, order, 2, 30);
  r.update(ms(4000));
  TEST_ASSERT_TRUE(r.heatLaps(3, ms(40000)));
  TEST_ASSERT_TRUE(r.info().heatState == HeatState::Countdown);
  TEST_ASSERT_EQUAL_INT64(ms(70000), r.info().heatGoUs);   // 30 s to get ready, counting down
  TEST_ASSERT_FALSE(r.update(ms(69000)));
  TEST_ASSERT_TRUE(r.startHeat(ms(50000)));             // ready earlier: start counting down now
  TEST_ASSERT_EQUAL_INT64(ms(54000), r.info().heatGoUs);
  TEST_ASSERT_FALSE(r.startHeat(ms(51000)));            // already counting down
  TEST_ASSERT_TRUE(r.update(ms(54000)));
  TEST_ASSERT_TRUE(r.counts(ms(55000)));
}

void test_no_heats_without_laps_or_order() {
  const uint8_t order[] = { 1, 2 };
  r.start(ms(0), 4000, 0, 5, order, 2);                // time race: no heats
  TEST_ASSERT_EQUAL_INT(0, r.info().nHeats);
  TEST_ASSERT_EQUAL_UINT8(0, r.heatDriver());
  r.start(ms(0), 4000, 10, 0);
  TEST_ASSERT_EQUAL_UINT8(0, r.heatDriver());
  r.update(ms(4000));
  TEST_ASSERT_FALSE(r.heatLaps(10, ms(5000)));
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
  r.heatLaps(3, ms(9000));
  r.start(ms(10000), 4000, 3, 0, order, 2);
  TEST_ASSERT_EQUAL_INT(0, r.info().heat);
  TEST_ASSERT_EQUAL_UINT8(1, r.heatDriver());
}

void test_back_heat() {
  const uint8_t order[] = { 1, 2 };
  r.start(ms(0), 4000, 3, 0, order, 2);
  r.backHeat();                                         // nothing before the first heat
  TEST_ASSERT_EQUAL_UINT8(1, r.heatDriver());
  r.update(ms(4000));
  r.heatLaps(3, ms(30000));
  TEST_ASSERT_EQUAL_UINT8(2, r.heatDriver());
  TEST_ASSERT_FALSE(r.counts(ms(31000)));
  r.backHeat();                                         // the pass that ended heat 1 was undone
  TEST_ASSERT_EQUAL_UINT8(1, r.heatDriver());
  TEST_ASSERT_TRUE(r.info().heatState == HeatState::Running);
  TEST_ASSERT_TRUE(r.counts(ms(32000)));                // driver 1 drives on
  TEST_ASSERT_FALSE(r.heatLaps(2, ms(33000)));          // its last lap was undone
  TEST_ASSERT_TRUE(r.heatLaps(3, ms(36000)));
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
  RUN_TEST(test_heats_in_order_manual_start);
  RUN_TEST(test_heats_prep_time);
  RUN_TEST(test_no_heats_without_laps_or_order);
  RUN_TEST(test_heats_skip_and_limits);
  RUN_TEST(test_restart_clears_heats);
  RUN_TEST(test_back_heat);
  return UNITY_END();
}
