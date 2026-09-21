#pragma once

#include <stdint.h>

// Content selected by a schedule; presentation is supplied by the controller.
enum class ScheduleView : uint8_t { kClock, kCountdown };

// Names the event at a boundary, rather than the phase approaching it.
enum class BoundaryKind : uint8_t {
  kFridayMidnight,
  kFridaySunset,
  kSaturdaySunset,
  kTradingOpen,
  kTradingClose,
};

// Identifies one occurrence in local wall-clock seconds (not a UTC instant).
struct ScheduleBoundary {
  BoundaryKind kind = BoundaryKind::kFridayMidnight;  // Event at the boundary.
  uint32_t atLocalSeconds = 0;  // Local wall-clock timestamp.
  uint8_t sessionIndex = 0;  // Trading session; ignored for Friday.
};

// Pure answer to what to show now and which boundary comes next.
struct ScheduleDecision {
  ScheduleView view = ScheduleView::kClock;  // Clock or countdown to next.
  ScheduleBoundary next;  // Strictly future boundary for valid schedule inputs.
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

// Friday sunsets belong to the week beginning at fridayMidnight.
ScheduleDecision evaluateFridaySchedule(uint32_t nowLocalSeconds,
                                        uint32_t fridayMidnight,
                                        uint32_t fridaySunset,
                                        uint32_t saturdaySunset);
uint32_t mostRecentFridayMidnight(uint32_t todayMidnight, uint8_t dayOfWeek);
bool isValidTradingSchedule(const TradingSchedule& schedule);
ScheduleDecision evaluateTradingSchedule(uint32_t nowLocalSeconds,
                                         uint32_t todayMidnight,
                                         uint8_t dayOfWeek,
                                         const TradingSchedule& schedule);
bool sameScheduleDecision(const ScheduleDecision& a, const ScheduleDecision& b);
