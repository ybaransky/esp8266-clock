#include <unity.h>

#include "sunset_calculator.h"

void setUp() {}
void tearDown() {}

namespace {

// Published sunset times are rounded to the minute and SolarCalculator is
// accurate to about a minute, so a few minutes of slack keeps the reference
// values honest without hiding a real error. A wrong calendar day is caught by
// the date assertions, not by this tolerance.
constexpr int kToleranceMinutes = 3;

void assertSunset(const DateTime& localDate, const Location& location,
                  int expectedHour, int expectedMinute) {
  const DateTime sunset = calculateSunset(localDate, location);
  TEST_ASSERT_EQUAL_INT(localDate.year(), sunset.year());
  TEST_ASSERT_EQUAL_UINT8(localDate.month(), sunset.month());
  TEST_ASSERT_EQUAL_UINT8(localDate.day(), sunset.day());
  const int actualMinutes = sunset.hour() * 60 + sunset.minute();
  const int expectedMinutes = expectedHour * 60 + expectedMinute;
  TEST_ASSERT_INT_WITHIN(kToleranceMinutes, expectedMinutes, actualMinutes);
}

}  // namespace

void test_west_of_utc_summer_and_winter_offsets() {
  const Location newYorkSummer{40.7128f, -74.0060f, -240};  // EDT
  const Location newYorkWinter{40.7128f, -74.0060f, -300};  // EST
  assertSunset(DateTime(2026, 6, 21), newYorkSummer, 20, 31);
  assertSunset(DateTime(2026, 12, 21), newYorkWinter, 16, 32);
}

// East of UTC, local midnight is the previous UTC day. The 18:00 local anchor
// is what keeps the calculation on the requested date; anchoring at midnight
// would return the previous day's sunset, which the date assertions reject.
void test_east_of_utc_stays_on_the_requested_date() {
  const Location tokyo{35.6762f, 139.6503f, 540};
  const Location sydney{-33.8688f, 151.2093f, 600};
  assertSunset(DateTime(2026, 6, 21), tokyo, 19, 0);
  assertSunset(DateTime(2026, 6, 21), sydney, 16, 54);
}

void test_invalid_coordinates_fall_back_to_local_evening() {
  const Location invalid{91.0f, 0.0f, 0};
  const DateTime sunset = calculateSunset(DateTime(2026, 3, 1), invalid);
  TEST_ASSERT_EQUAL_UINT8(1, sunset.day());
  TEST_ASSERT_EQUAL_UINT8(18, sunset.hour());
  TEST_ASSERT_EQUAL_UINT8(0, sunset.minute());
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_west_of_utc_summer_and_winter_offsets);
  RUN_TEST(test_east_of_utc_stays_on_the_requested_date);
  RUN_TEST(test_invalid_coordinates_fall_back_to_local_evening);
  return UNITY_END();
}
