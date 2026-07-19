#include "schedule.h"

namespace {

constexpr uint32_t kSecondsPerDay = 86400UL;
constexpr uint32_t kOpenSeconds = 9UL * 3600UL + 30UL * 60UL;
constexpr uint32_t kCloseSeconds = 16UL * 3600UL;

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

TradingBoundary evaluateTradingBoundary(uint32_t nowUnix,
                                        uint32_t todayMidnightUnix,
                                        uint8_t dayOfWeek) {
  const uint32_t openUnix = todayMidnightUnix + kOpenSeconds;
  const uint32_t closeUnix = todayMidnightUnix + kCloseSeconds;

  if (isTradingWeekday(dayOfWeek) && (nowUnix < openUnix)) {
    return {TradingPhase::kToOpen, openUnix};
  }
  if (isTradingWeekday(dayOfWeek) && (nowUnix < closeUnix)) {
    return {TradingPhase::kToClose, closeUnix};
  }

  const uint8_t daysAhead = daysUntilNextTradingDay(dayOfWeek);
  return {TradingPhase::kToOpen,
          todayMidnightUnix + daysAhead * kSecondsPerDay + kOpenSeconds};
}
