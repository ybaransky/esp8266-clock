#pragma once

#include <stdint.h>

enum class FridayPhase : uint8_t {
  kNone,
  kClock,
  kToFridaySunset,
  kToSaturdaySunset,
};

enum class TradingPhase : uint8_t {
  kNone,
  kToOpen,
  kToClose,
};

static constexpr uint8_t kMaxTradingIntervals = 2;

// A same-day Trading session expressed as minutes after local midnight.
struct TradingInterval {
  uint16_t startMinute = 0;
  uint16_t stopMinute = 0;
};

// Fixed-capacity set of enabled Trading sessions. Entries at and above
// intervalCount retain their configured values but are not scheduled.
struct TradingSchedule {
  uint8_t intervalCount = 1;
  TradingInterval intervals[kMaxTradingIntervals]{};
};

// Describes the next Trading boundary using local wall-clock Unix seconds.
struct TradingBoundary {
  TradingPhase phase = TradingPhase::kToOpen;  // Boundary being approached.
  uint32_t targetUnix = 0;  // Local wall-clock boundary timestamp.
  uint8_t intervalIndex = 0;  // Session whose open or close is targeted.
};

// Pure schedule calculations. They contain no Arduino, RTC, display, or I/O code.
FridayPhase evaluateFridayPhase(uint32_t nowUnix,
                                uint32_t fridaySunsetUnix,
                                uint32_t saturdaySunsetUnix);
uint32_t mostRecentFridayMidnight(uint32_t todayMidnightUnix,
                                  uint8_t dayOfWeek);
bool isValidTradingSchedule(const TradingSchedule& schedule);
TradingBoundary evaluateTradingBoundary(uint32_t nowUnix,
                                        uint32_t todayMidnightUnix,
                                        uint8_t dayOfWeek,
                                        const TradingSchedule& schedule);
