// Host tests for schedule.cpp: the pure Friday and Trading boundary math.
#include <unity.h>

#include <RTClib.h>

#include "schedule.h"

namespace {

// A concrete week to anchor the Friday tests. 2026-09-18 is a Friday.
constexpr uint32_t kFridayMidnight = 1789689600UL;                  // 2026-09-18 00:00
constexpr uint32_t kFridaySunset = kFridayMidnight + 19 * 3600UL;   // Fri 19:00
constexpr uint32_t kSaturdaySunset = kFridayMidnight + 43 * 3600UL; // Sat 19:00

TradingSchedule oneSession() {
  TradingSchedule s;
  s.intervalCount = 1;
  s.intervals[0] = {9 * 60 + 30, 16 * 60};
  s.intervals[1] = {17 * 60, 18 * 60};
  return s;
}

TradingSchedule twoSessions() {
  TradingSchedule s = oneSession();
  s.intervalCount = 2;
  return s;
}

// Confirms the stub calendar agrees with the constants the fixtures assume.
void test_fixture_weekdays_are_what_the_tests_assume() {
  TEST_ASSERT_EQUAL_UINT8(5, DateTime(kFridayMidnight).dayOfTheWeek());
  TEST_ASSERT_EQUAL_INT(2026, DateTime(kFridayMidnight).year());
  TEST_ASSERT_EQUAL_UINT8(9, DateTime(kFridayMidnight).month());
  TEST_ASSERT_EQUAL_UINT8(18, DateTime(kFridayMidnight).day());
}

void test_friday_before_midnight_shows_clock() {
  const ScheduleDecision d = evaluateFridaySchedule(
      kFridayMidnight - 3600UL, kFridayMidnight, kFridaySunset, kSaturdaySunset);
  TEST_ASSERT_EQUAL(ScheduleView::kClock, d.view);
  TEST_ASSERT_EQUAL(BoundaryKind::kFridayMidnight, d.next.kind);
  TEST_ASSERT_EQUAL_UINT32(kFridayMidnight, d.next.atLocalSeconds);
}

// Exactly on the boundary counts as after it: the countdown to Friday sunset
// starts at 00:00:00, not at 00:00:01.
void test_friday_midnight_belongs_to_the_following_interval() {
  const ScheduleDecision d = evaluateFridaySchedule(
      kFridayMidnight, kFridayMidnight, kFridaySunset, kSaturdaySunset);
  TEST_ASSERT_EQUAL(ScheduleView::kCountdown, d.view);
  TEST_ASSERT_EQUAL(BoundaryKind::kFridaySunset, d.next.kind);
}

void test_friday_sunset_switches_to_saturday_countdown() {
  const ScheduleDecision before = evaluateFridaySchedule(
      kFridaySunset - 1, kFridayMidnight, kFridaySunset, kSaturdaySunset);
  TEST_ASSERT_EQUAL(BoundaryKind::kFridaySunset, before.next.kind);

  const ScheduleDecision at = evaluateFridaySchedule(
      kFridaySunset, kFridayMidnight, kFridaySunset, kSaturdaySunset);
  TEST_ASSERT_EQUAL(ScheduleView::kCountdown, at.view);
  TEST_ASSERT_EQUAL(BoundaryKind::kSaturdaySunset, at.next.kind);
  TEST_ASSERT_EQUAL_UINT32(kSaturdaySunset, at.next.atLocalSeconds);
}

void test_after_saturday_sunset_shows_clock_until_next_friday() {
  const ScheduleDecision d = evaluateFridaySchedule(
      kSaturdaySunset, kFridayMidnight, kFridaySunset, kSaturdaySunset);
  TEST_ASSERT_EQUAL(ScheduleView::kClock, d.view);
  TEST_ASSERT_EQUAL(BoundaryKind::kFridayMidnight, d.next.kind);
  TEST_ASSERT_EQUAL_UINT32(kFridayMidnight + 7UL * 86400UL,
                           d.next.atLocalSeconds);
}

// Every phase must name a boundary strictly in the future, or the crossing
// detector in scheduled_mode.cpp can never fire.
void test_friday_targets_are_always_in_the_future() {
  for (uint32_t offset = 0; offset < 7UL * 86400UL; offset += 997UL) {
    const uint32_t now = kFridayMidnight - 86400UL + offset;
    const ScheduleDecision d = evaluateFridaySchedule(
        now, kFridayMidnight, kFridaySunset, kSaturdaySunset);
    TEST_ASSERT_TRUE_MESSAGE(d.next.atLocalSeconds > now,
                             "friday boundary must be strictly future");
  }
}

void test_most_recent_friday_midnight() {
  // From the Friday itself, the most recent Friday is today.
  TEST_ASSERT_EQUAL_UINT32(kFridayMidnight,
                           mostRecentFridayMidnight(kFridayMidnight, 5));
  // Saturday: yesterday.
  TEST_ASSERT_EQUAL_UINT32(
      kFridayMidnight, mostRecentFridayMidnight(kFridayMidnight + 86400UL, 6));
  // Sunday: two days back.
  TEST_ASSERT_EQUAL_UINT32(
      kFridayMidnight,
      mostRecentFridayMidnight(kFridayMidnight + 2UL * 86400UL, 0));
  // Thursday: six days back.
  TEST_ASSERT_EQUAL_UINT32(
      kFridayMidnight,
      mostRecentFridayMidnight(kFridayMidnight + 6UL * 86400UL, 4));
}

void test_valid_trading_schedules() {
  TEST_ASSERT_TRUE(isValidTradingSchedule(oneSession()));
  TEST_ASSERT_TRUE(isValidTradingSchedule(twoSessions()));
}

void test_invalid_trading_schedules_are_rejected() {
  TradingSchedule zero = oneSession();
  zero.intervalCount = 0;
  TEST_ASSERT_FALSE_MESSAGE(isValidTradingSchedule(zero), "zero sessions");

  TradingSchedule tooMany = oneSession();
  tooMany.intervalCount = kMaxTradingIntervals + 1;
  TEST_ASSERT_FALSE_MESSAGE(isValidTradingSchedule(tooMany), "too many");

  TradingSchedule inverted = oneSession();
  inverted.intervals[0] = {16 * 60, 9 * 60};
  TEST_ASSERT_FALSE_MESSAGE(isValidTradingSchedule(inverted), "stop before start");

  TradingSchedule empty = oneSession();
  empty.intervals[0] = {600, 600};
  TEST_ASSERT_FALSE_MESSAGE(isValidTradingSchedule(empty), "zero length");

  TradingSchedule outOfDay = oneSession();
  outOfDay.intervals[0] = {0, 1440};
  TEST_ASSERT_FALSE_MESSAGE(isValidTradingSchedule(outOfDay), "stop at 1440");

  TradingSchedule overlapping = twoSessions();
  overlapping.intervals[1] = {15 * 60, 17 * 60};
  TEST_ASSERT_FALSE_MESSAGE(isValidTradingSchedule(overlapping), "overlap");

  TradingSchedule touching = twoSessions();
  touching.intervals[1] = {16 * 60, 17 * 60};
  TEST_ASSERT_FALSE_MESSAGE(isValidTradingSchedule(touching), "not separated");
}

void test_trading_weekday_phases() {
  const uint32_t monday = kFridayMidnight + 3UL * 86400UL;  // 2026-09-21
  const TradingSchedule s = oneSession();
  TEST_ASSERT_EQUAL_UINT8(1, DateTime(monday).dayOfTheWeek());

  // Before the open: counting down to it.
  ScheduleDecision d = evaluateTradingSchedule(monday + 8 * 3600UL, monday, 1, s);
  TEST_ASSERT_EQUAL(BoundaryKind::kTradingOpen, d.next.kind);
  TEST_ASSERT_EQUAL_UINT32(monday + 9 * 3600UL + 1800UL, d.next.atLocalSeconds);

  // Exactly at the open the boundary belongs to the session, so the next
  // boundary is the close.
  d = evaluateTradingSchedule(monday + 9 * 3600UL + 1800UL, monday, 1, s);
  TEST_ASSERT_EQUAL(BoundaryKind::kTradingClose, d.next.kind);
  TEST_ASSERT_EQUAL_UINT32(monday + 16 * 3600UL, d.next.atLocalSeconds);

  // After the close: the next weekday open.
  d = evaluateTradingSchedule(monday + 20 * 3600UL, monday, 1, s);
  TEST_ASSERT_EQUAL(BoundaryKind::kTradingOpen, d.next.kind);
  TEST_ASSERT_EQUAL_UINT32(monday + 86400UL + 9 * 3600UL + 1800UL,
                           d.next.atLocalSeconds);
}

void test_trading_second_session_is_scheduled_when_enabled() {
  const uint32_t monday = kFridayMidnight + 3UL * 86400UL;
  const TradingSchedule s = twoSessions();

  const ScheduleDecision d =
      evaluateTradingSchedule(monday + 16 * 3600UL + 60UL, monday, 1, s);
  TEST_ASSERT_EQUAL(BoundaryKind::kTradingOpen, d.next.kind);
  TEST_ASSERT_EQUAL_UINT8(1, d.next.sessionIndex);
  TEST_ASSERT_EQUAL_UINT32(monday + 17 * 3600UL, d.next.atLocalSeconds);
}

// interval[1] keeps its configured times but must not be scheduled.
void test_trading_disabled_second_session_is_skipped() {
  const uint32_t monday = kFridayMidnight + 3UL * 86400UL;
  const TradingSchedule s = oneSession();

  const ScheduleDecision d =
      evaluateTradingSchedule(monday + 16 * 3600UL + 60UL, monday, 1, s);
  TEST_ASSERT_EQUAL(BoundaryKind::kTradingOpen, d.next.kind);
  TEST_ASSERT_EQUAL_UINT8(0, d.next.sessionIndex);
  TEST_ASSERT_EQUAL_UINT32(monday + 86400UL + 9 * 3600UL + 1800UL,
                           d.next.atLocalSeconds);
}

void test_trading_friday_rolls_over_to_monday() {
  const TradingSchedule s = oneSession();
  const ScheduleDecision d = evaluateTradingSchedule(
      kFridayMidnight + 20 * 3600UL, kFridayMidnight, 5, s);
  TEST_ASSERT_EQUAL(BoundaryKind::kTradingOpen, d.next.kind);
  TEST_ASSERT_EQUAL_UINT32(
      kFridayMidnight + 3UL * 86400UL + 9 * 3600UL + 1800UL,
      d.next.atLocalSeconds);
}

void test_trading_weekend_counts_down_to_monday() {
  const TradingSchedule s = oneSession();
  const uint32_t saturday = kFridayMidnight + 86400UL;
  const uint32_t sunday = kFridayMidnight + 2UL * 86400UL;

  ScheduleDecision d = evaluateTradingSchedule(saturday + 3600UL, saturday, 6, s);
  TEST_ASSERT_EQUAL(ScheduleView::kCountdown, d.view);
  TEST_ASSERT_EQUAL_UINT32(saturday + 2UL * 86400UL + 9 * 3600UL + 1800UL,
                           d.next.atLocalSeconds);

  d = evaluateTradingSchedule(sunday + 3600UL, sunday, 0, s);
  TEST_ASSERT_EQUAL_UINT32(sunday + 86400UL + 9 * 3600UL + 1800UL,
                           d.next.atLocalSeconds);
}

// Trading never renders a clock. scheduled_mode.cpp relies on this when it maps
// a decision to a ViewState, so pin it down here rather than by inspection.
void test_trading_always_counts_down() {
  const TradingSchedule s = twoSessions();
  for (uint8_t dow = 0; dow < 7; ++dow) {
    const uint32_t midnight = kFridayMidnight + dow * 86400UL;
    for (uint32_t secondOfDay = 0; secondOfDay < 86400UL; secondOfDay += 211UL) {
      const ScheduleDecision d =
          evaluateTradingSchedule(midnight + secondOfDay, midnight, dow, s);
      TEST_ASSERT_EQUAL_MESSAGE(ScheduleView::kCountdown, d.view,
                                "trading must always be a countdown");
      TEST_ASSERT_TRUE_MESSAGE(d.next.atLocalSeconds > midnight + secondOfDay,
                               "trading boundary must be strictly future");
    }
  }
}

void test_same_schedule_decision_compares_every_field() {
  const ScheduleDecision a{ScheduleView::kCountdown,
                           {BoundaryKind::kTradingOpen, 1000, 0}};
  ScheduleDecision b = a;
  TEST_ASSERT_TRUE(sameScheduleDecision(a, b));

  b = a;
  b.view = ScheduleView::kClock;
  TEST_ASSERT_FALSE(sameScheduleDecision(a, b));
  b = a;
  b.next.kind = BoundaryKind::kTradingClose;
  TEST_ASSERT_FALSE(sameScheduleDecision(a, b));
  b = a;
  b.next.atLocalSeconds = 1001;
  TEST_ASSERT_FALSE(sameScheduleDecision(a, b));
  b = a;
  b.next.sessionIndex = 1;
  TEST_ASSERT_FALSE(sameScheduleDecision(a, b));
}

}  // namespace

void setUp() {}
void tearDown() {}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_fixture_weekdays_are_what_the_tests_assume);
  RUN_TEST(test_friday_before_midnight_shows_clock);
  RUN_TEST(test_friday_midnight_belongs_to_the_following_interval);
  RUN_TEST(test_friday_sunset_switches_to_saturday_countdown);
  RUN_TEST(test_after_saturday_sunset_shows_clock_until_next_friday);
  RUN_TEST(test_friday_targets_are_always_in_the_future);
  RUN_TEST(test_most_recent_friday_midnight);
  RUN_TEST(test_valid_trading_schedules);
  RUN_TEST(test_invalid_trading_schedules_are_rejected);
  RUN_TEST(test_trading_weekday_phases);
  RUN_TEST(test_trading_second_session_is_scheduled_when_enabled);
  RUN_TEST(test_trading_disabled_second_session_is_skipped);
  RUN_TEST(test_trading_friday_rolls_over_to_monday);
  RUN_TEST(test_trading_weekend_counts_down_to_monday);
  RUN_TEST(test_trading_always_counts_down);
  RUN_TEST(test_same_schedule_decision_compares_every_field);
  return UNITY_END();
}
