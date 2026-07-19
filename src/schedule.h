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

// Describes the next Trading boundary using local wall-clock Unix seconds.
struct TradingBoundary {
  TradingPhase phase = TradingPhase::kToOpen;  // Boundary being approached.
  uint32_t targetUnix = 0;  // Local wall-clock boundary timestamp.
};

// Pure schedule calculations. They contain no Arduino, RTC, display, or I/O code.
FridayPhase evaluateFridayPhase(uint32_t nowUnix,
                                uint32_t fridaySunsetUnix,
                                uint32_t saturdaySunsetUnix);
uint32_t mostRecentFridayMidnight(uint32_t todayMidnightUnix,
                                  uint8_t dayOfWeek);
TradingBoundary evaluateTradingBoundary(uint32_t nowUnix,
                                        uint32_t todayMidnightUnix,
                                        uint8_t dayOfWeek);
