// Host unit tests for src/nec.cpp: pio test -e native
#include <unity.h>
#include "nec.h"

static NecDecoder dec;
static int64_t t;

// Plays a frame into the decoder like the IR receiver would. stretch = µs added to every mark
// (and taken from every space), as real receivers do. Returns decoded codes count, last in `code`.
static int play(uint32_t value, int stretch, uint32_t& code) {
  uint16_t d[68];
  int n = necEncode(value, d);
  int found = 0;
  for (int i = 0; i < n; i++) {
    bool mark = (i % 2) == 0;
    if (dec.edge(mark, t, code)) found++;
    t += d[i] + (mark ? stretch : -stretch);
  }
  if (dec.edge(false, t, code)) found++;  // the final mark ends (carrier off)
  t += 10000;                             // pause between frames
  return found;
}

void setUp() { dec.reset(); t = 1000000; }
void tearDown() {}

void test_roundtrip_ricino_id() {
  uint32_t code = 0;
  TEST_ASSERT_EQUAL(1, play(0x1B39, 0, code));
  TEST_ASSERT_EQUAL_HEX32(0x1B39, code);
  TEST_ASSERT_EQUAL_HEX16(0x1B39, necTransponderId(code));
}

void test_roundtrip_all_bits() {
  uint32_t code = 0;
  TEST_ASSERT_EQUAL(1, play(0xA5C3F00F, 0, code));
  TEST_ASSERT_EQUAL_HEX32(0xA5C3F00F, code);
}

void test_tolerates_receiver_stretch() {
  uint32_t code = 0;
  TEST_ASSERT_EQUAL(1, play(0x2468, 150, code));
  TEST_ASSERT_EQUAL_HEX32(0x2468, code);
  TEST_ASSERT_EQUAL(1, play(0x2468, -120, code));
}

void test_back_to_back_frames() {
  uint32_t code = 0;
  TEST_ASSERT_EQUAL(1, play(0x1000, 0, code));
  TEST_ASSERT_EQUAL(1, play(0x1001, 0, code));
  TEST_ASSERT_EQUAL_HEX32(0x1001, code);
}

void test_noise_before_frame_is_ignored() {
  uint32_t code = 0;
  dec.edge(true, t, code);  t += 300;     // short glitch
  dec.edge(false, t, code); t += 2000;
  TEST_ASSERT_EQUAL(1, play(0x3333, 0, code));
  TEST_ASSERT_EQUAL_HEX32(0x3333, code);
}

void test_truncated_frame_is_dropped_and_next_decodes() {
  uint16_t d[68];
  int n = necEncode(0x4444, d);
  uint32_t code = 0;
  for (int i = 0; i < n / 2; i++) { dec.edge(i % 2 == 0, t, code); t += d[i]; }   // car left the zone
  t += 30000;
  TEST_ASSERT_EQUAL(1, play(0x5555, 0, code));
  TEST_ASSERT_EQUAL_HEX32(0x5555, code);
}

void test_transponder_id_range() {
  TEST_ASSERT_EQUAL(0, necTransponderId(0x0FFF));        // below the RiCino range
  TEST_ASSERT_EQUAL(0, necTransponderId(0x00FF1000));    // high bits set: some TV remote
  TEST_ASSERT_EQUAL_HEX16(0xFFFF, necTransponderId(0xFFFF));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_roundtrip_ricino_id);
  RUN_TEST(test_roundtrip_all_bits);
  RUN_TEST(test_tolerates_receiver_stretch);
  RUN_TEST(test_back_to_back_frames);
  RUN_TEST(test_noise_before_frame_is_ignored);
  RUN_TEST(test_truncated_frame_is_dropped_and_next_decodes);
  RUN_TEST(test_transponder_id_range);
  return UNITY_END();
}
