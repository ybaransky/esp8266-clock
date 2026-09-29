// Host tests for timezone_rule.cpp: POSIX TZ parsing and UTC/local conversion.
//
// Transition instants are checked to the second on both sides, because an
// off-by-one-second or wrong-offset bug here moves every clock digit and every
// schedule boundary by an hour twice a year.
#include <unity.h>

#include <stdio.h>
#include <string.h>

#include <RTClib.h>

#include "timezone_rule.h"

namespace {

constexpr int32_t kHour = 3600;

const char* const kNewYork = "EST5EDT,M3.2.0,M11.1.0";
const char* const kLondon = "GMT0BST,M3.5.0/1,M10.5.0";
const char* const kSydney = "AEST-10AEDT,M10.1.0,M4.1.0/3";

uint32_t at(int year, int month, int day, int hour, int minute, int second) {
  return DateTime(year, month, day, hour, minute, second).unixtime();
}

TimeZoneRule parsed(const char* text) {
  TimeZoneRule rule;
  TEST_ASSERT_TRUE_MESSAGE(parsePosixTimeZone(text, &rule), text);
  return rule;
}

// -- Parsing -------------------------------------------------------------------

void test_parses_new_york() {
  const TimeZoneRule rule = parsed(kNewYork);
  TEST_ASSERT_EQUAL_INT32(-5 * kHour, rule.stdOffsetSeconds);
  TEST_ASSERT_EQUAL_INT32(-4 * kHour, rule.dstOffsetSeconds);
  TEST_ASSERT_TRUE(rule.hasDst);
  TEST_ASSERT_EQUAL_STRING("EST", rule.stdName);
  TEST_ASSERT_EQUAL_STRING("EDT", rule.dstName);
  TEST_ASSERT_EQUAL_UINT8(3, rule.dstStart.month);
  TEST_ASSERT_EQUAL_UINT8(2, rule.dstStart.week);
  TEST_ASSERT_EQUAL_INT32(2 * kHour, rule.dstStart.timeSeconds);
}

void test_parses_zone_without_daylight_time() {
  const TimeZoneRule rule = parsed("IST-5:30");
  TEST_ASSERT_EQUAL_INT32(5 * kHour + 30 * 60, rule.stdOffsetSeconds);
  TEST_ASSERT_FALSE(rule.hasDst);
  uint32_t next = 0;
  TEST_ASSERT_FALSE(nextTransitionAfter(rule, at(2026, 6, 1, 0, 0, 0), &next));
}

void test_parses_angle_bracket_names_and_quarter_hours() {
  const TimeZoneRule chatham =
      parsed("<+1245>-12:45<+1345>,M9.5.0/2:45,M4.1.0/3:45");
  TEST_ASSERT_EQUAL_STRING("+1245", chatham.stdName);
  TEST_ASSERT_EQUAL_STRING("+1345", chatham.dstName);
  TEST_ASSERT_EQUAL_INT32(12 * kHour + 45 * 60, chatham.stdOffsetSeconds);
  TEST_ASSERT_EQUAL_INT32(13 * kHour + 45 * 60, chatham.dstOffsetSeconds);
  TEST_ASSERT_EQUAL_INT32(2 * kHour + 45 * 60, chatham.dstStart.timeSeconds);
}

void test_rejects_malformed_rules() {
  const char* const kBad[] = {
      "",                                // empty
      "EST",                             // no offset
      "5",                               // no name
      "ES5",                             // unquoted name shorter than 3
      "EST25",                           // offset beyond 24h
      "EST5EDT",                         // DST without rules
      "EST5EDT,M3.2.0",                  // missing end rule
      "EST5EDT,M13.2.0,M11.1.0",         // month 13
      "EST5EDT,M3.0.0,M11.1.0",          // week 0
      "EST5EDT,M3.2.7,M11.1.0",          // weekday 7
      "EST5EDT,J60,M11.1.0",             // Jn form is not supported
      "EST5EDT,M3.2.0,M11.1.0 trailing", // trailing text
      "<>0",                             // empty quoted name
      "<ABCDEFGH>0",                     // name too long
      "EST5:60",                         // minutes out of range
  };
  for (const char* text : kBad) {
    TimeZoneRule rule;
    rule.stdOffsetSeconds = 12345;
    TEST_ASSERT_FALSE_MESSAGE(parsePosixTimeZone(text, &rule), text);
    TEST_ASSERT_EQUAL_INT32_MESSAGE(12345, rule.stdOffsetSeconds, text);  // untouched
  }
  TEST_ASSERT_FALSE(parsePosixTimeZone(nullptr, nullptr));
}

// Every zone the web picker can send must parse. The table is read from the
// vendored CSV so a refreshed table is covered without editing this test.
void test_every_vendored_zone_parses() {
  FILE* file = fopen("assets/timezones.csv", "r");
  TEST_ASSERT_NOT_NULL_MESSAGE(file, "run from the project root");
  char line[160];
  int count = 0;
  while (fgets(line, sizeof(line), file) != nullptr) {
    // "Zone/Name","POSIX": the rule is between the third and fourth quotes.
    char* ruleStart = strchr(line, ',');
    TEST_ASSERT_NOT_NULL(ruleStart);
    ruleStart += 2;
    char* ruleEnd = strrchr(ruleStart, '"');
    TEST_ASSERT_NOT_NULL(ruleEnd);
    *ruleEnd = '\0';
    TEST_ASSERT_TRUE_MESSAGE(strlen(ruleStart) < kTimezoneRuleLength, ruleStart);
    TimeZoneRule rule;
    TEST_ASSERT_TRUE_MESSAGE(parsePosixTimeZone(ruleStart, &rule), line);
    ++count;
  }
  fclose(file);
  TEST_ASSERT_TRUE(count > 400);
}

// -- Transitions -------------------------------------------------------------

void test_new_york_falls_back_on_2026_11_01() {
  const TimeZoneRule rule = parsed(kNewYork);
  const uint32_t before = 1793512770;  // 2026-11-01T05:59:30Z
  TEST_ASSERT_EQUAL_UINT32(at(2026, 11, 1, 5, 59, 30), before);
  TEST_ASSERT_EQUAL_UINT32(at(2026, 11, 1, 1, 59, 30), localFromUtc(rule, before));
  TEST_ASSERT_EQUAL_STRING("EDT", abbreviationAt(rule, before));
  TEST_ASSERT_EQUAL_UINT32(at(2026, 11, 1, 1, 59, 59), localFromUtc(rule, before + 29));
  TEST_ASSERT_EQUAL_UINT32(at(2026, 11, 1, 1, 0, 0), localFromUtc(rule, before + 30));
  TEST_ASSERT_EQUAL_STRING("EST", abbreviationAt(rule, before + 30));
}

void test_new_york_springs_forward_on_2027_03_14() {
  const TimeZoneRule rule = parsed(kNewYork);
  const uint32_t before = 1805007570;  // 2027-03-14T06:59:30Z
  TEST_ASSERT_EQUAL_UINT32(at(2027, 3, 14, 6, 59, 30), before);
  TEST_ASSERT_EQUAL_UINT32(at(2027, 3, 14, 1, 59, 59), localFromUtc(rule, before + 29));
  TEST_ASSERT_EQUAL_UINT32(at(2027, 3, 14, 3, 0, 0), localFromUtc(rule, before + 30));
  TEST_ASSERT_EQUAL_INT32(-4 * kHour, utcOffsetSecondsAt(rule, before + 30));
}

void test_london_changes_at_one_utc_on_last_sundays() {
  const TimeZoneRule rule = parsed(kLondon);
  TEST_ASSERT_EQUAL_INT32(0, utcOffsetSecondsAt(rule, at(2026, 3, 29, 0, 59, 59)));
  TEST_ASSERT_EQUAL_INT32(kHour, utcOffsetSecondsAt(rule, at(2026, 3, 29, 1, 0, 0)));
  TEST_ASSERT_EQUAL_INT32(kHour, utcOffsetSecondsAt(rule, at(2026, 10, 25, 0, 59, 59)));
  TEST_ASSERT_EQUAL_INT32(0, utcOffsetSecondsAt(rule, at(2026, 10, 25, 1, 0, 0)));
}

void test_sydney_daylight_time_spans_new_year() {
  const TimeZoneRule rule = parsed(kSydney);
  TEST_ASSERT_EQUAL_INT32(11 * kHour, utcOffsetSecondsAt(rule, at(2026, 1, 15, 0, 0, 0)));
  TEST_ASSERT_EQUAL_INT32(10 * kHour, utcOffsetSecondsAt(rule, at(2026, 7, 15, 0, 0, 0)));
  TEST_ASSERT_EQUAL_INT32(11 * kHour, utcOffsetSecondsAt(rule, at(2026, 12, 31, 12, 0, 0)));
  // Ends 03:00 AEDT on Sunday 2026-04-05, which is 16:00Z the day before.
  TEST_ASSERT_EQUAL_INT32(11 * kHour, utcOffsetSecondsAt(rule, at(2026, 4, 4, 15, 59, 59)));
  TEST_ASSERT_EQUAL_INT32(10 * kHour, utcOffsetSecondsAt(rule, at(2026, 4, 4, 16, 0, 0)));
}

void test_lord_howe_has_half_hour_daylight_time() {
  const TimeZoneRule rule = parsed("<+1030>-10:30<+11>-11,M10.1.0,M4.1.0");
  TEST_ASSERT_EQUAL_INT32(11 * kHour, utcOffsetSecondsAt(rule, at(2026, 1, 15, 0, 0, 0)));
  TEST_ASSERT_EQUAL_INT32(10 * kHour + 30 * 60,
                          utcOffsetSecondsAt(rule, at(2026, 7, 15, 0, 0, 0)));
  TEST_ASSERT_EQUAL_STRING("+11", abbreviationAt(rule, at(2026, 1, 15, 0, 0, 0)));
}

void test_jerusalem_transition_time_past_midnight() {
  // M3.4.4/26: the fourth Thursday of March at 26:00, i.e. Friday 02:00 IST.
  // In 2026 that is Friday March 27 02:00 IST = 00:00Z.
  const TimeZoneRule rule = parsed("IST-2IDT,M3.4.4/26,M10.5.0");
  TEST_ASSERT_EQUAL_INT32(2 * kHour, utcOffsetSecondsAt(rule, at(2026, 3, 26, 23, 59, 59)));
  TEST_ASSERT_EQUAL_INT32(3 * kHour, utcOffsetSecondsAt(rule, at(2026, 3, 27, 0, 0, 0)));
}

void test_dublin_negative_daylight_time() {
  // Irish Standard Time is the summer offset; winter is the "daylight" GMT.
  const TimeZoneRule rule = parsed("IST-1GMT0,M10.5.0,M3.5.0/1");
  TEST_ASSERT_EQUAL_INT32(kHour, utcOffsetSecondsAt(rule, at(2026, 7, 1, 12, 0, 0)));
  TEST_ASSERT_EQUAL_INT32(0, utcOffsetSecondsAt(rule, at(2026, 12, 1, 12, 0, 0)));
  TEST_ASSERT_EQUAL_STRING("GMT", abbreviationAt(rule, at(2026, 12, 1, 12, 0, 0)));
}

void test_next_transition_after() {
  const TimeZoneRule rule = parsed(kNewYork);
  uint32_t next = 0;
  TEST_ASSERT_TRUE(nextTransitionAfter(rule, at(2026, 9, 29, 12, 0, 0), &next));
  TEST_ASSERT_EQUAL_UINT32(at(2026, 11, 1, 6, 0, 0), next);
  TEST_ASSERT_TRUE(nextTransitionAfter(rule, next, &next));  // strictly after
  TEST_ASSERT_EQUAL_UINT32(at(2027, 3, 14, 7, 0, 0), next);
}

// -- Local to UTC ------------------------------------------------------------

void test_utc_from_local_round_trips_ordinary_times() {
  const TimeZoneRule rule = parsed(kNewYork);
  const uint32_t summerNoon = at(2026, 7, 4, 12, 0, 0);
  const uint32_t winterNoon = at(2026, 1, 4, 12, 0, 0);
  TEST_ASSERT_EQUAL_UINT32(at(2026, 7, 4, 16, 0, 0), utcFromLocal(rule, summerNoon));
  TEST_ASSERT_EQUAL_UINT32(at(2026, 1, 4, 17, 0, 0), utcFromLocal(rule, winterNoon));
  TEST_ASSERT_EQUAL_UINT32(summerNoon, localFromUtc(rule, utcFromLocal(rule, summerNoon)));
}

void test_utc_from_local_in_spring_gap_moves_forward() {
  // 02:30 does not exist on 2027-03-14; it resolves to 03:30 EDT (07:30Z).
  const TimeZoneRule rule = parsed(kNewYork);
  TEST_ASSERT_EQUAL_UINT32(at(2027, 3, 14, 7, 30, 0),
                           utcFromLocal(rule, at(2027, 3, 14, 2, 30, 0)));
}

void test_utc_from_local_in_fall_overlap_takes_first() {
  // 01:30 happens twice on 2026-11-01; the first is EDT (05:30Z).
  const TimeZoneRule rule = parsed(kNewYork);
  TEST_ASSERT_EQUAL_UINT32(at(2026, 11, 1, 5, 30, 0),
                           utcFromLocal(rule, at(2026, 11, 1, 1, 30, 0)));
}

// -- Fixed-offset rules --------------------------------------------------------

void test_fixed_offset_rules_round_trip() {
  const struct {
    int minutes;           // East-positive offset.
    const char* expected;  // Rule text written for it.
  } kCases[] = {
      {-240, "<-04>4"}, {330, "<+0530>-5:30"}, {0, "UTC0"}, {-210, "<-0330>3:30"},
      {840, "<+14>-14"},
  };
  for (const auto& testCase : kCases) {
    char text[kTimezoneRuleLength];
    formatFixedOffsetRule(testCase.minutes, text, sizeof(text));
    TEST_ASSERT_EQUAL_STRING(testCase.expected, text);
    const TimeZoneRule rule = parsed(text);
    TEST_ASSERT_FALSE(rule.hasDst);
    TEST_ASSERT_EQUAL_INT32(testCase.minutes * 60, rule.stdOffsetSeconds);
  }
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parses_new_york);
  RUN_TEST(test_parses_zone_without_daylight_time);
  RUN_TEST(test_parses_angle_bracket_names_and_quarter_hours);
  RUN_TEST(test_rejects_malformed_rules);
  RUN_TEST(test_every_vendored_zone_parses);
  RUN_TEST(test_new_york_falls_back_on_2026_11_01);
  RUN_TEST(test_new_york_springs_forward_on_2027_03_14);
  RUN_TEST(test_london_changes_at_one_utc_on_last_sundays);
  RUN_TEST(test_sydney_daylight_time_spans_new_year);
  RUN_TEST(test_lord_howe_has_half_hour_daylight_time);
  RUN_TEST(test_jerusalem_transition_time_past_midnight);
  RUN_TEST(test_dublin_negative_daylight_time);
  RUN_TEST(test_next_transition_after);
  RUN_TEST(test_utc_from_local_round_trips_ordinary_times);
  RUN_TEST(test_utc_from_local_in_spring_gap_moves_forward);
  RUN_TEST(test_utc_from_local_in_fall_overlap_takes_first);
  RUN_TEST(test_fixed_offset_rules_round_trip);
  return UNITY_END();
}
