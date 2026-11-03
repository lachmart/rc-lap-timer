// Host unit tests for src/laps.cpp: pio test -e native
#include <unity.h>
#include "laps.h"

static constexpr uint32_t MIN_LAP = 2000;   // ms
static LapSnapshot snap;

static int64_t s(double seconds) { return (int64_t)(seconds * 1e6) + 1; }   // +1: 0 means "not started"

static const LapSnapshot& take(double atSeconds = 100) {
  lapsSnapshot(snap, s(atSeconds));
  return snap;
}

static const DriverStats* statsOf(uint8_t id) {
  for (int i = 0; i < snap.nStats; i++) if (snap.stats[i].id == id) return &snap.stats[i];
  return nullptr;
}

void setUp() {
  lapsSetDriver(0);
  lapsReset();
}
void tearDown() {}

// ---------------------------------------------------------------- basic timing
void test_first_pass_only_starts() {
  lapsCrossing(s(1), MIN_LAP);
  take(3.5);
  TEST_ASSERT_TRUE(snap.running);
  TEST_ASSERT_EQUAL(0, snap.count);
  TEST_ASSERT_EQUAL(2500, snap.curMs);
  TEST_ASSERT_TRUE(snap.canUndo);
}

void test_second_pass_records_lap_rounded_to_ms() {
  lapsCrossing(s(1), MIN_LAP);
  lapsCrossing(s(1) + 8123600, MIN_LAP);   // 8123.6 ms
  take();
  TEST_ASSERT_EQUAL(1, snap.count);
  TEST_ASSERT_EQUAL(8124, snap.laps[0].ms);
  TEST_ASSERT_EQUAL(8124, snap.bestMs);
}

void test_pass_shorter_than_min_lap_is_ignored() {
  lapsCrossing(s(1), MIN_LAP);
  lapsCrossing(s(2.5), MIN_LAP);
  lapsCrossing(s(9), MIN_LAP);
  take();
  TEST_ASSERT_EQUAL(1, snap.ignored);
  TEST_ASSERT_EQUAL(1, snap.count);
  TEST_ASSERT_EQUAL(8000, snap.laps[0].ms);   // measured from the start, not from the ignored pass
}

void test_best_is_minimum() {
  lapsCrossing(s(0), MIN_LAP);
  lapsCrossing(s(10), MIN_LAP);
  lapsCrossing(s(17), MIN_LAP);
  lapsCrossing(s(26), MIN_LAP);
  take();
  TEST_ASSERT_EQUAL(3, snap.count);
  TEST_ASSERT_EQUAL(7000, snap.bestMs);
}

void test_reset_clears_session_but_keeps_driver() {
  lapsSetDriver(3);
  lapsCrossing(s(0), MIN_LAP);
  lapsCrossing(s(10), MIN_LAP);
  lapsReset();
  take();
  TEST_ASSERT_FALSE(snap.running);
  TEST_ASSERT_EQUAL(0, snap.count);
  TEST_ASSERT_EQUAL(0, snap.nStats);
  TEST_ASSERT_EQUAL(0, snap.bestMs);
  TEST_ASSERT_EQUAL(3, snap.driver);
  TEST_ASSERT_FALSE(snap.canUndo);
}

void test_revision_changes_on_crossing() {
  uint32_t before = lapsRevision();
  lapsCrossing(s(0), MIN_LAP);
  TEST_ASSERT_NOT_EQUAL(before, lapsRevision());
}

// ---------------------------------------------------------------- drivers
void test_laps_are_attributed_to_driver_and_stats_kept() {
  lapsSetDriver(1);
  lapsCrossing(s(0), MIN_LAP);
  lapsCrossing(s(10), MIN_LAP);
  lapsCrossing(s(18), MIN_LAP);
  lapsSetDriver(2);
  lapsCrossing(s(30), MIN_LAP);
  lapsCrossing(s(39), MIN_LAP);
  take();

  TEST_ASSERT_EQUAL(3, snap.count);
  TEST_ASSERT_EQUAL(1, snap.laps[0].driver);
  TEST_ASSERT_EQUAL(2, snap.laps[2].driver);

  const DriverStats* a = statsOf(1);
  const DriverStats* b = statsOf(2);
  TEST_ASSERT_NOT_NULL(a);
  TEST_ASSERT_NOT_NULL(b);
  TEST_ASSERT_EQUAL(2, a->count);
  TEST_ASSERT_EQUAL(8000, a->bestMs);
  TEST_ASSERT_EQUAL(18000, a->totalMs);
  TEST_ASSERT_EQUAL(1, b->count);
  TEST_ASSERT_EQUAL(9000, b->bestMs);
  TEST_ASSERT_EQUAL(8000, snap.bestMs);
}

void test_switching_driver_drops_lap_in_progress() {
  lapsSetDriver(1);
  lapsCrossing(s(0), MIN_LAP);
  lapsSetDriver(2);
  take(5);
  TEST_ASSERT_FALSE(snap.running);
  lapsCrossing(s(6), MIN_LAP);              // new driver's start pass
  lapsCrossing(s(15), MIN_LAP);
  take();
  TEST_ASSERT_EQUAL(1, snap.count);
  TEST_ASSERT_EQUAL(9000, snap.laps[0].ms);
}

void test_selecting_same_driver_keeps_lap() {
  lapsSetDriver(1);
  lapsCrossing(s(0), MIN_LAP);
  lapsSetDriver(1);
  take(5);
  TEST_ASSERT_TRUE(snap.running);
}

void test_forget_driver_drops_stats() {
  lapsSetDriver(1);
  lapsCrossing(s(0), MIN_LAP);
  lapsCrossing(s(10), MIN_LAP);
  lapsForgetDriver(1);
  take();
  TEST_ASSERT_NULL(statsOf(1));
  TEST_ASSERT_EQUAL(0, snap.bestMs);
}

// ---------------------------------------------------------------- corrections
void test_undo_false_trigger_continues_lap_from_previous_pass() {
  lapsCrossing(s(0), MIN_LAP);
  lapsCrossing(s(10), MIN_LAP);             // real lap: 10 s
  lapsCrossing(s(14), MIN_LAP);             // false trigger mid-lap: "4 s lap"
  TEST_ASSERT_TRUE(lapsUndo());
  take(15);
  TEST_ASSERT_EQUAL(1, snap.count);
  TEST_ASSERT_EQUAL(5000, snap.curMs);      // current lap runs from 10 s, not from 14 s
  TEST_ASSERT_EQUAL(10000, snap.bestMs);
  TEST_ASSERT_FALSE(snap.canUndo);

  lapsCrossing(s(19), MIN_LAP);             // real end of the lap
  take();
  TEST_ASSERT_EQUAL(2, snap.count);
  TEST_ASSERT_EQUAL(9000, snap.laps[1].ms);
}

void test_undo_restores_best_and_stats() {
  lapsSetDriver(1);
  lapsCrossing(s(0), MIN_LAP);
  lapsCrossing(s(10), MIN_LAP);
  lapsCrossing(s(13), MIN_LAP);             // false 3 s "best"
  take();
  TEST_ASSERT_EQUAL(3000, snap.bestMs);
  lapsUndo();
  take();
  TEST_ASSERT_EQUAL(10000, snap.bestMs);
  TEST_ASSERT_EQUAL(1, statsOf(1)->count);
  TEST_ASSERT_EQUAL(10000, statsOf(1)->totalMs);
}

void test_undo_start_pass() {
  lapsCrossing(s(0), MIN_LAP);
  TEST_ASSERT_TRUE(lapsUndo());
  take();
  TEST_ASSERT_FALSE(snap.running);
}

void test_undo_only_once_and_not_without_pass() {
  TEST_ASSERT_FALSE(lapsUndo());
  lapsCrossing(s(0), MIN_LAP);
  lapsCrossing(s(10), MIN_LAP);
  TEST_ASSERT_TRUE(lapsUndo());
  TEST_ASSERT_FALSE(lapsUndo());
}

void test_ignored_pass_does_not_change_undo_target() {
  lapsCrossing(s(0), MIN_LAP);
  lapsCrossing(s(10), MIN_LAP);
  lapsCrossing(s(11), MIN_LAP);             // ignored (< min lap)
  TEST_ASSERT_TRUE(lapsUndo());             // still undoes the 10 s lap
  take();
  TEST_ASSERT_EQUAL(0, snap.count);
  TEST_ASSERT_EQUAL(100000, snap.curMs);    // timing from the start pass again
}

void test_restart_drops_lap_in_progress() {
  lapsCrossing(s(0), MIN_LAP);
  lapsCrossing(s(10), MIN_LAP);
  lapsRestart();
  take();
  TEST_ASSERT_FALSE(snap.running);
  TEST_ASSERT_EQUAL(1, snap.count);         // finished laps stay
  TEST_ASSERT_FALSE(snap.canUndo);
  lapsCrossing(s(50), MIN_LAP);             // start pass
  lapsCrossing(s(58), MIN_LAP);
  take();
  TEST_ASSERT_EQUAL(8000, snap.laps[1].ms);
}

void test_delete_lap_updates_stats_and_best() {
  lapsSetDriver(1);
  lapsCrossing(s(0), MIN_LAP);
  lapsCrossing(s(5), MIN_LAP);              // lap 1: 5 s (car cut the track)
  lapsCrossing(s(13), MIN_LAP);             // lap 2: 8 s
  lapsCrossing(s(22), MIN_LAP);             // lap 3: 9 s
  TEST_ASSERT_TRUE(lapsDelete(1));
  take();
  TEST_ASSERT_EQUAL(3, snap.count);         // numbering stays
  TEST_ASSERT_TRUE(snap.laps[0].deleted);
  TEST_ASSERT_EQUAL(8000, snap.bestMs);
  TEST_ASSERT_EQUAL(2, statsOf(1)->count);
  TEST_ASSERT_EQUAL(17000, statsOf(1)->totalMs);
  TEST_ASSERT_EQUAL(8000, statsOf(1)->bestMs);
}

void test_delete_invalid_or_twice() {
  lapsCrossing(s(0), MIN_LAP);
  lapsCrossing(s(10), MIN_LAP);
  TEST_ASSERT_FALSE(lapsDelete(0));
  TEST_ASSERT_FALSE(lapsDelete(2));
  TEST_ASSERT_TRUE(lapsDelete(1));
  TEST_ASSERT_FALSE(lapsDelete(1));
}

void test_delete_last_lap_disables_undo() {
  lapsCrossing(s(0), MIN_LAP);
  lapsCrossing(s(10), MIN_LAP);
  lapsDelete(1);
  take();
  TEST_ASSERT_FALSE(snap.canUndo);
  TEST_ASSERT_TRUE(snap.running);           // timing continues
}

void test_join_merges_lap_split_by_false_trigger() {
  lapsSetDriver(1);
  lapsCrossing(s(0), MIN_LAP);
  lapsCrossing(s(9), MIN_LAP);              // lap 1: 9 s
  lapsCrossing(s(12.5), MIN_LAP);           // false trigger → lap 2: 3.5 s
  lapsCrossing(s(18), MIN_LAP);             // lap 3: 5.5 s, the rest of the real lap
  lapsCrossing(s(27), MIN_LAP);             // lap 4: 9 s
  take();
  TEST_ASSERT_EQUAL(3500, snap.bestMs);

  TEST_ASSERT_TRUE(lapsJoin(2));
  take();
  TEST_ASSERT_EQUAL(3, snap.count);         // the false pass is gone, later laps move down
  TEST_ASSERT_EQUAL(9000, snap.laps[1].ms);
  TEST_ASSERT_FALSE(snap.laps[1].deleted);
  TEST_ASSERT_EQUAL(9000, snap.laps[2].ms);
  TEST_ASSERT_EQUAL(9000, snap.bestMs);
  TEST_ASSERT_EQUAL(3, statsOf(1)->count);
  TEST_ASSERT_EQUAL(27000, statsOf(1)->totalMs);
}

void test_join_last_lap_keeps_timing_and_undo() {
  lapsCrossing(s(0), MIN_LAP);
  lapsCrossing(s(9), MIN_LAP);
  lapsCrossing(s(12), MIN_LAP);             // false trigger
  lapsCrossing(s(18), MIN_LAP);
  TEST_ASSERT_TRUE(lapsJoin(2));
  take(20);
  TEST_ASSERT_EQUAL(2, snap.count);
  TEST_ASSERT_EQUAL(9000, snap.laps[1].ms);
  TEST_ASSERT_EQUAL(2000, snap.curMs);      // current lap still timed from the 18 s pass
  TEST_ASSERT_TRUE(lapsUndo());             // undo the 18 s pass: the merged lap goes back to running
  take(20);
  TEST_ASSERT_EQUAL(1, snap.count);
  TEST_ASSERT_EQUAL(11000, snap.curMs);
}

void test_join_rejects_non_contiguous_or_mixed_laps() {
  lapsSetDriver(1);
  lapsCrossing(s(0), MIN_LAP);
  lapsCrossing(s(9), MIN_LAP);              // lap 1
  lapsRestart();
  lapsCrossing(s(20), MIN_LAP);
  lapsCrossing(s(29), MIN_LAP);             // lap 2, not contiguous with lap 1
  TEST_ASSERT_FALSE(lapsJoin(1));
  lapsCrossing(s(38), MIN_LAP);             // lap 3
  lapsDelete(3);
  TEST_ASSERT_FALSE(lapsJoin(2));           // deleted
  TEST_ASSERT_FALSE(lapsJoin(3));           // no next lap
  TEST_ASSERT_FALSE(lapsJoin(0));

  lapsSetDriver(2);                         // driver switch also breaks contiguity
  lapsCrossing(s(40), MIN_LAP);
  lapsCrossing(s(49), MIN_LAP);             // lap 4, driver 2
  TEST_ASSERT_FALSE(lapsJoin(3));
}

void test_contiguity_tolerates_ms_rounding() {
  lapsCrossing(s(0), MIN_LAP);
  lapsCrossing(s(0) + 8000400, MIN_LAP);    // 8000.4 ms → 8000
  lapsCrossing(s(0) + 11000900, MIN_LAP);   // 3000.5 ms → 3001
  take();
  TEST_ASSERT_TRUE(lapsContinues(snap.laps[0], snap.laps[1]));
}

// ---------------------------------------------------------------- ring buffer
void test_undo_and_join_with_full_ring_never_expose_overwritten_laps() {
  int64_t t = s(0);
  lapsCrossing(t, MIN_LAP);
  for (int i = 1; i <= MAX_LAPS + 10; i++) { t += (10000 + i) * 1000LL; lapsCrossing(t, MIN_LAP); }
  take(100000);
  uint32_t oldest = snap.laps[0].ms;
  TEST_ASSERT_TRUE(lapsUndo());
  take(100000);
  TEST_ASSERT_EQUAL(MAX_LAPS - 1, snap.n);  // one fewer kept, not an overwritten slot brought back
  TEST_ASSERT_EQUAL(oldest, snap.laps[0].ms);
  TEST_ASSERT_TRUE(lapsJoin(snap.count - 5));
  take(100000);
  TEST_ASSERT_EQUAL(MAX_LAPS - 2, snap.n);
  TEST_ASSERT_EQUAL(oldest, snap.laps[0].ms);

  t += 9000000; lapsCrossing(t, MIN_LAP);   // new laps fill the ring again
  t += 9000000; lapsCrossing(t, MIN_LAP);
  t += 9000000; lapsCrossing(t, MIN_LAP);
  take(100000);
  TEST_ASSERT_EQUAL(MAX_LAPS, snap.n);
  TEST_ASSERT_EQUAL(9000, snap.laps[MAX_LAPS - 1].ms);
}

void test_ring_buffer_keeps_last_laps_and_stats_of_all() {
  lapsSetDriver(1);
  const int total = MAX_LAPS + 25;
  int64_t t = s(0);
  lapsCrossing(t, MIN_LAP);
  for (int i = 1; i <= total; i++) {                        // lap i takes 10 s + i ms
    t += (10000 + i) * 1000LL;
    lapsCrossing(t, MIN_LAP);
  }
  take(100000);
  TEST_ASSERT_EQUAL(total, snap.count);
  TEST_ASSERT_EQUAL(MAX_LAPS, snap.n);
  TEST_ASSERT_EQUAL(10026, snap.laps[0].ms);                // oldest kept = lap 26
  TEST_ASSERT_EQUAL(10000 + total, snap.laps[MAX_LAPS - 1].ms);
  TEST_ASSERT_EQUAL(total, statsOf(1)->count);              // stats include rotated-out laps
  TEST_ASSERT_EQUAL(10001, statsOf(1)->bestMs);

  TEST_ASSERT_FALSE(lapsDelete(1));                         // no longer in memory
  TEST_ASSERT_TRUE(lapsDelete(26));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_first_pass_only_starts);
  RUN_TEST(test_second_pass_records_lap_rounded_to_ms);
  RUN_TEST(test_pass_shorter_than_min_lap_is_ignored);
  RUN_TEST(test_best_is_minimum);
  RUN_TEST(test_reset_clears_session_but_keeps_driver);
  RUN_TEST(test_revision_changes_on_crossing);
  RUN_TEST(test_laps_are_attributed_to_driver_and_stats_kept);
  RUN_TEST(test_switching_driver_drops_lap_in_progress);
  RUN_TEST(test_selecting_same_driver_keeps_lap);
  RUN_TEST(test_forget_driver_drops_stats);
  RUN_TEST(test_undo_false_trigger_continues_lap_from_previous_pass);
  RUN_TEST(test_undo_restores_best_and_stats);
  RUN_TEST(test_undo_start_pass);
  RUN_TEST(test_undo_only_once_and_not_without_pass);
  RUN_TEST(test_ignored_pass_does_not_change_undo_target);
  RUN_TEST(test_restart_drops_lap_in_progress);
  RUN_TEST(test_delete_lap_updates_stats_and_best);
  RUN_TEST(test_delete_invalid_or_twice);
  RUN_TEST(test_delete_last_lap_disables_undo);
  RUN_TEST(test_join_merges_lap_split_by_false_trigger);
  RUN_TEST(test_join_last_lap_keeps_timing_and_undo);
  RUN_TEST(test_join_rejects_non_contiguous_or_mixed_laps);
  RUN_TEST(test_contiguity_tolerates_ms_rounding);
  RUN_TEST(test_ring_buffer_keeps_last_laps_and_stats_of_all);
  RUN_TEST(test_undo_and_join_with_full_ring_never_expose_overwritten_laps);
  return UNITY_END();
}
