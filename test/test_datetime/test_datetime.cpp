// Host tests for datetime_validation.cpp: calendar validation and parsing.
#include <unity.h>

#include <string.h>

#include <RTClib.h>

#include "datetime_validation.h"

namespace {

void test_valid_dates() {
  TEST_ASSERT_TRUE(isValidDate(2026, 9, 18));
  TEST_ASSERT_TRUE(isValidDate(2000, 1, 1));
  TEST_ASSERT_TRUE(isValidDate(2099, 12, 31));
}

void test_year_range_is_2000_to_2099() {
  TEST_ASSERT_FALSE(isValidDate(1999, 12, 31));
  TEST_ASSERT_FALSE(isValidDate(2100, 1, 1));
}

void test_month_and_day_bounds() {
  TEST_ASSERT_FALSE(isValidDate(2026, 0, 15));
  TEST_ASSERT_FALSE(isValidDate(2026, 13, 15));
  TEST_ASSERT_FALSE(isValidDate(2026, 1, 0));
  TEST_ASSERT_FALSE(isValidDate(2026, 1, 32));
  TEST_ASSERT_FALSE(isValidDate(2026, 4, 31));  // April has 30
  TEST_ASSERT_TRUE(isValidDate(2026, 4, 30));
}

void test_leap_years() {
  // 2024 is a leap year; 2026 is not.
  TEST_ASSERT_TRUE(isValidDate(2024, 2, 29));
  TEST_ASSERT_FALSE(isValidDate(2026, 2, 29));
  TEST_ASSERT_TRUE(isValidDate(2026, 2, 28));
  // 2000 is a leap year (divisible by 400); 2100 is out of range anyway.
  TEST_ASSERT_TRUE(isValidDate(2000, 2, 29));
}

void test_time_bounds() {
  TEST_ASSERT_TRUE(isValidTime(0, 0, 0));
  TEST_ASSERT_TRUE(isValidTime(23, 59, 59));
  TEST_ASSERT_FALSE(isValidTime(24, 0, 0));
  TEST_ASSERT_FALSE(isValidTime(-1, 0, 0));
  TEST_ASSERT_FALSE(isValidTime(0, 60, 0));
  TEST_ASSERT_FALSE(isValidTime(0, 0, 60));
}

void test_parse_iso_date() {
  int year = 0, month = 0, day = 0;
  TEST_ASSERT_TRUE(parseIsoDate("2026-09-18", &year, &month, &day));
  TEST_ASSERT_EQUAL_INT(2026, year);
  TEST_ASSERT_EQUAL_INT(9, month);
  TEST_ASSERT_EQUAL_INT(18, day);

  TEST_ASSERT_FALSE(parseIsoDate("2026-9-18", &year, &month, &day));
  TEST_ASSERT_FALSE(parseIsoDate("2026/09/18", &year, &month, &day));
  TEST_ASSERT_FALSE(parseIsoDate("2026-09-1", &year, &month, &day));
  TEST_ASSERT_FALSE(parseIsoDate("20x6-09-18", &year, &month, &day));
  TEST_ASSERT_FALSE(parseIsoDate(nullptr, &year, &month, &day));
}

void test_parse_clock_time_accepts_both_lengths() {
  int hour = 0, minute = 0, second = 99;
  TEST_ASSERT_TRUE(parseClockTime("14:05", &hour, &minute, &second));
  TEST_ASSERT_EQUAL_INT(14, hour);
  TEST_ASSERT_EQUAL_INT(5, minute);
  TEST_ASSERT_EQUAL_INT_MESSAGE(0, second, "omitted seconds become zero");

  TEST_ASSERT_TRUE(parseClockTime("14:05:30", &hour, &minute, &second));
  TEST_ASSERT_EQUAL_INT(30, second);

  TEST_ASSERT_FALSE(parseClockTime("1405", &hour, &minute, &second));
  TEST_ASSERT_FALSE(parseClockTime("14.05", &hour, &minute, &second));
  TEST_ASSERT_FALSE(parseClockTime("24:00", &hour, &minute, &second));
}

void test_parse_local_datetime_accepts_space_and_t() {
  DateTime parsed;
  TEST_ASSERT_TRUE(parseLocalDateTime("2026-09-18 14:05:30", parsed));
  TEST_ASSERT_EQUAL_INT(2026, parsed.year());
  TEST_ASSERT_EQUAL_UINT8(9, parsed.month());
  TEST_ASSERT_EQUAL_UINT8(18, parsed.day());
  TEST_ASSERT_EQUAL_UINT8(14, parsed.hour());
  TEST_ASSERT_EQUAL_UINT8(5, parsed.minute());
  TEST_ASSERT_EQUAL_UINT8(30, parsed.second());

  DateTime withT;
  TEST_ASSERT_TRUE(parseLocalDateTime("2026-09-18T14:05:30", withT));
  TEST_ASSERT_EQUAL_UINT32(parsed.unixtime(), withT.unixtime());
}

void test_parse_local_datetime_without_seconds() {
  DateTime parsed;
  TEST_ASSERT_TRUE(parseLocalDateTime("2026-09-18 14:05", parsed));
  TEST_ASSERT_EQUAL_UINT8(0, parsed.second());
}

void test_parse_local_datetime_rejects_malformed_input() {
  DateTime parsed;
  TEST_ASSERT_FALSE(parseLocalDateTime("", parsed));
  TEST_ASSERT_FALSE(parseLocalDateTime(nullptr, parsed));
  TEST_ASSERT_FALSE(parseLocalDateTime("2026-09-18", parsed));
  TEST_ASSERT_FALSE(parseLocalDateTime("2026-09-18_14:05", parsed));
  TEST_ASSERT_FALSE(parseLocalDateTime("2026-02-29 00:00:00", parsed));
  TEST_ASSERT_FALSE(parseLocalDateTime("1999-01-01 00:00:00", parsed));
  TEST_ASSERT_FALSE(parseLocalDateTime("2026-09-18 25:00:00", parsed));
}

// formatLocalDateTime is the inverse of parseLocalDateTime: resolveCountupStart
// writes a config field with the first and every reader parses it back with the
// second, so a disagreement between them would persist an origin that no longer
// loads and silently fall back to the default.
void test_format_local_datetime_is_canonical() {
  char text[kLocalDateTimeLength];
  formatLocalDateTime(DateTime(2026, 9, 18, 14, 5, 30), text, sizeof(text));
  TEST_ASSERT_EQUAL_STRING("2026-09-18 14:05:30", text);

  // Every field zero-padded to its full width, including a single-digit year
  // position that snprintf would otherwise shorten.
  formatLocalDateTime(DateTime(2000, 1, 2, 3, 4, 5), text, sizeof(text));
  TEST_ASSERT_EQUAL_STRING("2000-01-02 03:04:05", text);

  formatLocalDateTime(DateTime(2099, 12, 31, 23, 59, 59), text, sizeof(text));
  TEST_ASSERT_EQUAL_STRING("2099-12-31 23:59:59", text);
}

void test_format_local_datetime_round_trips() {
  const uint32_t samples[] = {
      DateTime(2026, 9, 18, 14, 5, 30).unixtime(),  // ordinary
      DateTime(2024, 2, 29, 0, 0, 0).unixtime(),    // leap day, midnight
      DateTime(2000, 1, 1, 0, 0, 0).unixtime(),     // lower bound
      DateTime(2099, 12, 31, 23, 59, 59).unixtime() // upper bound
  };
  for (size_t i = 0; i < sizeof(samples) / sizeof(samples[0]); ++i) {
    char text[kLocalDateTimeLength];
    formatLocalDateTime(DateTime(samples[i]), text, sizeof(text));
    DateTime parsed;
    TEST_ASSERT_TRUE_MESSAGE(parseLocalDateTime(text, parsed), text);
    TEST_ASSERT_EQUAL_UINT32(samples[i], parsed.unixtime());
  }
}

// kLocalDateTimeLength is exactly the canonical width plus a terminator, so a
// CountupConfig::start-sized buffer must not lose the seconds.
void test_format_local_datetime_needs_no_more_than_its_constant() {
  TEST_ASSERT_EQUAL_size_t(20, kLocalDateTimeLength);
  char exact[kLocalDateTimeLength];
  formatLocalDateTime(DateTime(2026, 9, 18, 14, 5, 30), exact, sizeof(exact));
  TEST_ASSERT_EQUAL_size_t(19, strlen(exact));
}

// A short buffer truncates rather than overruns. The truncated text must not
// parse back, so a caller that ignored the size cannot persist a half-written
// origin that silently reads as a different instant.
void test_format_local_datetime_truncates_safely() {
  char small[12];
  memset(small, 'x', sizeof(small));
  formatLocalDateTime(DateTime(2026, 9, 18, 14, 5, 30), small, sizeof(small));
  TEST_ASSERT_EQUAL_size_t(11, strlen(small));
  TEST_ASSERT_EQUAL_STRING("2026-09-18 ", small);
  DateTime parsed;
  TEST_ASSERT_FALSE(parseLocalDateTime(small, parsed));

  // A zero size and a null destination are both no-ops, not writes.
  char untouched[4] = "abc";
  formatLocalDateTime(DateTime(2026, 9, 18, 14, 5, 30), untouched, 0);
  TEST_ASSERT_EQUAL_STRING("abc", untouched);
  formatLocalDateTime(DateTime(2026, 9, 18, 14, 5, 30), nullptr, 8);
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_valid_dates);
  RUN_TEST(test_year_range_is_2000_to_2099);
  RUN_TEST(test_month_and_day_bounds);
  RUN_TEST(test_leap_years);
  RUN_TEST(test_time_bounds);
  RUN_TEST(test_parse_iso_date);
  RUN_TEST(test_parse_clock_time_accepts_both_lengths);
  RUN_TEST(test_parse_local_datetime_accepts_space_and_t);
  RUN_TEST(test_parse_local_datetime_without_seconds);
  RUN_TEST(test_parse_local_datetime_rejects_malformed_input);
  RUN_TEST(test_format_local_datetime_is_canonical);
  RUN_TEST(test_format_local_datetime_round_trips);
  RUN_TEST(test_format_local_datetime_needs_no_more_than_its_constant);
  RUN_TEST(test_format_local_datetime_truncates_safely);
  return UNITY_END();
}
