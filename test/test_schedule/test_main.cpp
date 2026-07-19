#include <unity.h>

#include "schedule.h"

namespace {

constexpr uint32_t kDay = 86400UL;
constexpr uint32_t kOpen = 9UL * 3600UL + 30UL * 60UL;
constexpr uint32_t kClose = 16UL * 3600UL;

void testFridayPhasesFollowSunsetBoundaries() {
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(FridayPhase::kToFridaySunset),
      static_cast<uint8_t>(evaluateFridayPhase(99, 100, 200)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(FridayPhase::kToSaturdaySunset),
      static_cast<uint8_t>(evaluateFridayPhase(100, 100, 200)));
  TEST_ASSERT_EQUAL_UINT8(
      static_cast<uint8_t>(FridayPhase::kClock),
      static_cast<uint8_t>(evaluateFridayPhase(200, 100, 200)));
}

void testMostRecentFridayMidnightUsesRtcWeekdayConvention() {
  const uint32_t today = 10 * kDay;
  TEST_ASSERT_EQUAL_UINT32(today, mostRecentFridayMidnight(today, 5));
  TEST_ASSERT_EQUAL_UINT32(today - kDay,
                           mostRecentFridayMidnight(today, 6));
  TEST_ASSERT_EQUAL_UINT32(today - 2 * kDay,
                           mostRecentFridayMidnight(today, 0));
  TEST_ASSERT_EQUAL_UINT32(today - 6 * kDay,
                           mostRecentFridayMidnight(today, 4));
}

void testTradingWeekdayMovesFromOpenToClose() {
  const uint32_t monday = 20 * kDay;
  TradingBoundary boundary =
      evaluateTradingBoundary(monday + kOpen - 1, monday, 1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TradingPhase::kToOpen),
                          static_cast<uint8_t>(boundary.phase));
  TEST_ASSERT_EQUAL_UINT32(monday + kOpen, boundary.targetUnix);

  boundary = evaluateTradingBoundary(monday + kOpen, monday, 1);
  TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(TradingPhase::kToClose),
                          static_cast<uint8_t>(boundary.phase));
  TEST_ASSERT_EQUAL_UINT32(monday + kClose, boundary.targetUnix);
}

void testTradingFridayCloseAndWeekendTargetMondayOpen() {
  const uint32_t friday = 30 * kDay;
  TradingBoundary boundary =
      evaluateTradingBoundary(friday + kClose, friday, 5);
  TEST_ASSERT_EQUAL_UINT32(friday + 3 * kDay + kOpen, boundary.targetUnix);

  const uint32_t saturday = friday + kDay;
  boundary = evaluateTradingBoundary(saturday + 12 * 3600UL, saturday, 6);
  TEST_ASSERT_EQUAL_UINT32(saturday + 2 * kDay + kOpen,
                           boundary.targetUnix);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(testFridayPhasesFollowSunsetBoundaries);
  RUN_TEST(testMostRecentFridayMidnightUsesRtcWeekdayConvention);
  RUN_TEST(testTradingWeekdayMovesFromOpenToClose);
  RUN_TEST(testTradingFridayCloseAndWeekendTargetMondayOpen);
  return UNITY_END();
}
