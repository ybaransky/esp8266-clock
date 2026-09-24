#include "schedule.h"

namespace {

constexpr uint32_t kSecondsPerDay = 86400UL;

bool isTradingWeekday(uint8_t dayOfWeek) {
  return (dayOfWeek >= 1) && (dayOfWeek <= 5);
}

// Minutes after local midnight, as a second offset.
//
// Explicitly uint32_t rather than `minute * 60UL`: `unsigned long` is 32 bits
// on the ESP8266 but 64 on an x86_64 host, so a UL literal widened the whole
// surrounding expression only when compiled for the host tests. That both
// produced a narrowing warning where the result lands in a braced initializer,
// and - the part that actually matters - stopped those tests from modelling the
// device's 32-bit wraparound, since the host was computing in 64 bits.
constexpr uint32_t secondsFromMidnight(uint16_t minute) {
  return static_cast<uint32_t>(minute) * 60U;
}

uint8_t daysUntilNextTradingDay(uint8_t dayOfWeek) {
  uint8_t daysAhead = 1;
  uint8_t candidate = (dayOfWeek + daysAhead) % 7;
  while (!isTradingWeekday(candidate)) {
    ++daysAhead;
    candidate = (dayOfWeek + daysAhead) % 7;
  }
  return daysAhead;
}

}  // namespace

ScheduleDecision evaluateFridaySchedule(uint32_t nowLocalSeconds,
                                        uint32_t fridayMidnight,
                                        uint32_t fridaySunset,
                                        uint32_t saturdaySunset) {
  if (nowLocalSeconds < fridayMidnight) {
    return {ScheduleView::kClock, {BoundaryKind::kFridayMidnight, fridayMidnight, 0}};
  }
  if (nowLocalSeconds < fridaySunset) {
    return {ScheduleView::kCountdown, {BoundaryKind::kFridaySunset, fridaySunset, 0}};
  }
  if (nowLocalSeconds < saturdaySunset) {
    return {ScheduleView::kCountdown, {BoundaryKind::kSaturdaySunset, saturdaySunset, 0}};
  }
  return {ScheduleView::kClock,
          {BoundaryKind::kFridayMidnight, fridayMidnight + 7 * kSecondsPerDay, 0}};
}

uint32_t mostRecentFridayMidnight(uint32_t todayMidnight,
                                  uint8_t dayOfWeek) {
  const uint8_t daysSinceFriday =
      (dayOfWeek >= 5) ? (dayOfWeek - 5) : (dayOfWeek + 2);
  return todayMidnight - daysSinceFriday * kSecondsPerDay;
}

bool isValidTradingSchedule(const TradingSchedule& schedule) {
  if ((schedule.intervalCount < 1) ||
      (schedule.intervalCount > kMaxTradingIntervals)) {
    return false;
  }
  for (uint8_t i = 0; i < schedule.intervalCount; ++i) {
    const TradingInterval& interval = schedule.intervals[i];
    if ((interval.startMinute >= 1440) || (interval.stopMinute >= 1440) ||
        (interval.startMinute >= interval.stopMinute)) {
      return false;
    }
    if ((i > 0) &&
        (schedule.intervals[i - 1].stopMinute >= interval.startMinute)) {
      return false;
    }
  }
  return true;
}

ScheduleDecision evaluateTradingSchedule(uint32_t nowLocalSeconds,
                                        uint32_t todayMidnight,
                                        uint8_t dayOfWeek,
                                        const TradingSchedule& schedule) {
  if (isTradingWeekday(dayOfWeek)) {
    for (uint8_t i = 0; i < schedule.intervalCount; ++i) {
      const uint32_t openAt =
          todayMidnight + secondsFromMidnight(schedule.intervals[i].startMinute);
      const uint32_t closeAt =
          todayMidnight + secondsFromMidnight(schedule.intervals[i].stopMinute);
      if (nowLocalSeconds < openAt) {
        return {ScheduleView::kCountdown, {BoundaryKind::kTradingOpen, openAt, i}};
      }
      if (nowLocalSeconds < closeAt) {
        return {ScheduleView::kCountdown, {BoundaryKind::kTradingClose, closeAt, i}};
      }
    }
  }

  const uint8_t daysAhead = daysUntilNextTradingDay(dayOfWeek);
  const uint32_t nextOpen = todayMidnight + daysAhead * kSecondsPerDay +
                            secondsFromMidnight(schedule.intervals[0].startMinute);
  return {ScheduleView::kCountdown,
          {BoundaryKind::kTradingOpen, nextOpen, 0}};
}

bool sameScheduleDecision(const ScheduleDecision& a, const ScheduleDecision& b) {
  return (a.view == b.view) && (a.next.kind == b.next.kind) &&
         (a.next.atLocalSeconds == b.next.atLocalSeconds) &&
         (a.next.sessionIndex == b.next.sessionIndex);
}
