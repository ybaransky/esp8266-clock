#include "schedule.h"

namespace {

constexpr uint32_t kSecondsPerDay = 86400UL;

bool isTradingWeekday(uint8_t dayOfWeek) {
  return (dayOfWeek >= 1) && (dayOfWeek <= 5);
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

FridayPhase evaluateFridayPhase(uint32_t nowUnix,
                                uint32_t fridaySunsetUnix,
                                uint32_t saturdaySunsetUnix) {
  if (nowUnix < fridaySunsetUnix) return FridayPhase::kToFridaySunset;
  if (nowUnix < saturdaySunsetUnix) return FridayPhase::kToSaturdaySunset;
  return FridayPhase::kClock;
}

uint32_t mostRecentFridayMidnight(uint32_t todayMidnightUnix,
                                  uint8_t dayOfWeek) {
  const uint8_t daysSinceFriday =
      (dayOfWeek >= 5) ? (dayOfWeek - 5) : (dayOfWeek + 2);
  return todayMidnightUnix - daysSinceFriday * kSecondsPerDay;
}

bool isValidTradingSchedule(const TradingSchedule& schedule) {
  if ((schedule.intervalCount < 1) ||
      (schedule.intervalCount > kMaxTradingIntervals)) {
    return false;
  }
  for (uint8_t i = 0; i < kMaxTradingIntervals; ++i) {
    const TradingInterval& interval = schedule.intervals[i];
    if ((interval.startMinute >= 1440) || (interval.stopMinute >= 1440) ||
        (interval.startMinute >= interval.stopMinute)) {
      return false;
    }
    if ((i > 0) && (i < schedule.intervalCount) &&
        (schedule.intervals[i - 1].stopMinute >= interval.startMinute)) {
      return false;
    }
  }
  return true;
}

TradingBoundary evaluateTradingBoundary(uint32_t nowUnix,
                                        uint32_t todayMidnightUnix,
                                        uint8_t dayOfWeek,
                                        const TradingSchedule& schedule) {
  if (isTradingWeekday(dayOfWeek)) {
    for (uint8_t i = 0; i < schedule.intervalCount; ++i) {
      const uint32_t openUnix =
          todayMidnightUnix + schedule.intervals[i].startMinute * 60UL;
      const uint32_t closeUnix =
          todayMidnightUnix + schedule.intervals[i].stopMinute * 60UL;
      if (nowUnix < openUnix) return {TradingPhase::kToOpen, openUnix};
      if (nowUnix < closeUnix) return {TradingPhase::kToClose, closeUnix};
    }
  }

  const uint8_t daysAhead = daysUntilNextTradingDay(dayOfWeek);
  return {TradingPhase::kToOpen,
          todayMidnightUnix + daysAhead * kSecondsPerDay +
              schedule.intervals[0].startMinute * 60UL};
}
