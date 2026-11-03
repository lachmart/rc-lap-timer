// Host unit tests for src/zroundproto.cpp: pio test -e native
// The texts follow RiCino's Receiver.ino, which ZRound (>= 1.15) talks to.
#include <unity.h>
#include "zroundproto.h"

static ZRoundProto z;
static std::string out;

static int64_t ms(int64_t v) { return v * 1000; }

static void send(const char* s, int64_t nowUs) { for (; *s; s++) z.feed(*s, nowUs); }

void setUp() { z = ZRoundProto(); out.clear(); }
void tearDown() {}

void test_idle_says_nothing() {
  z.poll(ms(100000), out);
  z.passing(0x1234, ms(1000), out);
  TEST_ASSERT_EQUAL_STRING("", out.c_str());
  TEST_ASSERT_FALSE(z.connected());
  TEST_ASSERT_FALSE(z.racing());
}

void test_connect_acks_21_times_100ms_apart() {
  send("%C&", ms(1000));
  TEST_ASSERT_TRUE(z.connected());
  int acks = 0;
  for (int64_t t = 1000; t <= 5000; t += 10) {
    std::string o;
    z.poll(ms(t), o);
    if (!o.empty()) {
      TEST_ASSERT_EQUAL_STRING("%A&", o.c_str());
      TEST_ASSERT_EQUAL_INT64(1000 + acks * 100, t);
      acks++;
    }
  }
  TEST_ASSERT_EQUAL_INT(21, acks);
}

void test_race_time_sync_every_5s() {
  send("%I&", ms(2000));
  TEST_ASSERT_TRUE(z.racing());
  z.poll(ms(6999), out);
  TEST_ASSERT_EQUAL_STRING("", out.c_str());
  z.poll(ms(7000), out);
  TEST_ASSERT_EQUAL_STRING("%T1388&\r\n", out.c_str());           // 5000 ms
  out.clear();
  z.poll(ms(12500), out);
  TEST_ASSERT_EQUAL_STRING("%T2710&\r\n", out.c_str());           // 10000 ms
}

void test_passing_in_ms_since_start() {
  send("%I&", ms(2000));
  z.passing(0x1A2B, ms(2000 + 61234), out);
  TEST_ASSERT_EQUAL_STRING("%L1A2B,EF32&", out.c_str());
}

void test_passing_ignored_outside_race_or_before_start_or_without_id() {
  z.passing(0x1A2B, ms(3000), out);                                 // no race
  send("%I&", ms(2000));
  z.passing(0x1A2B, ms(1999), out);                                 // before %I&
  z.passing(0, ms(3000), out);                                      // beam-only passing
  send("%F&", ms(4000));
  TEST_ASSERT_FALSE(z.racing());
  z.passing(0x1A2B, ms(5000), out);
  z.poll(ms(20000), out);                                           // no more time syncs
  TEST_ASSERT_EQUAL_STRING("", out.c_str());
}

void test_parser_skips_noise_and_too_long_messages() {
  send("xx&C&%TOOLONGMSG&%&", 0);
  TEST_ASSERT_FALSE(z.connected());
  send("garbage%C&", 0);
  TEST_ASSERT_TRUE(z.connected());
}

void test_restart_with_new_start() {
  send("%I&", ms(1000));
  send("%I&", ms(9000));
  z.passing(0x1001, ms(10000), out);
  TEST_ASSERT_EQUAL_STRING("%L1001,3E8&", out.c_str());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_idle_says_nothing);
  RUN_TEST(test_connect_acks_21_times_100ms_apart);
  RUN_TEST(test_race_time_sync_every_5s);
  RUN_TEST(test_passing_in_ms_since_start);
  RUN_TEST(test_passing_ignored_outside_race_or_before_start_or_without_id);
  RUN_TEST(test_parser_skips_noise_and_too_long_messages);
  RUN_TEST(test_restart_with_new_start);
  return UNITY_END();
}
