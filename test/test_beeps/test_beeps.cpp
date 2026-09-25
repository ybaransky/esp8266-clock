#include <unity.h>

#include "beep_pattern.h"

void setUp() {}
void tearDown() {}

void test_beeps_end_their_slots_and_never_exceed_100ms() {
  TEST_ASSERT_FALSE(boundaryBeepOn(399, 40000, 2));
  TEST_ASSERT_TRUE(boundaryBeepOn(400, 40000, 2));
  TEST_ASSERT_TRUE(boundaryBeepOn(499, 40000, 2));
  TEST_ASSERT_FALSE(boundaryBeepOn(500, 40000, 2));
  // Fourth phase: 16 Hz, first slot is 62 ms, tone occupies its last 31 ms.
  TEST_ASSERT_FALSE(boundaryBeepOn(30030, 40000, 2));
  TEST_ASSERT_TRUE(boundaryBeepOn(30031, 40000, 2));
  TEST_ASSERT_FALSE(boundaryBeepOn(30062, 40000, 2));
}

void test_rate_doubles_in_each_phase() {
  for (uint32_t phase = 0; phase < 4; ++phase) {
    unsigned starts = 0;
    bool previous = false;
    for (uint32_t ms = phase * 10000; ms < (phase + 1) * 10000; ++ms) {
      const bool on = boundaryBeepOn(ms, 40000, 2);
      if (on && !previous) ++starts;
      previous = on;
    }
    TEST_ASSERT_EQUAL_UINT32(20U << phase, starts);
  }
}

void test_boundary_end_and_supported_extremes() {
  const uint32_t durations[] = {4000, 5000, 40000, 1200000};
  const uint8_t rates[] = {1, 2, 10};
  for (uint32_t duration : durations) {
    for (uint8_t rate : rates) {
      TEST_ASSERT_FALSE(boundaryBeepOn(0, duration, rate));
      TEST_ASSERT_TRUE(boundaryBeepOn(duration - 1, duration, rate));
      TEST_ASSERT_FALSE(boundaryBeepOn(duration, duration, rate));
      TEST_ASSERT_FALSE(boundaryBeepOn(duration + 5000, duration, rate));
    }
  }
  TEST_ASSERT_FALSE(boundaryBeepOn(0, 0, 2));
  TEST_ASSERT_FALSE(boundaryBeepOn(499, 40000, 0));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_beeps_end_their_slots_and_never_exceed_100ms);
  RUN_TEST(test_rate_doubles_in_each_phase);
  RUN_TEST(test_boundary_end_and_supported_extremes);
  return UNITY_END();
}
