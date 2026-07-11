#pragma once

#include <Arduino.h>
#include <RTClib.h>

#include "config.h"
#include "display_manager.h"

enum class FridayPhase : uint8_t {
  kNone,
  kClock,
  kToFridaySunset,
  kToSaturdaySunset,
};

struct FridayScheduleResult {
  FridayPhase phase = FridayPhase::kClock;
  ViewState view;
};

// Pure Friday-mode policy: resolves the current phase and desired base view
// from explicit inputs. It performs no I/O and does not mutate display state.
FridayScheduleResult evaluateFridaySchedule(
    const DateTime& now,
    const DateTime& fridaySunset,
    const DateTime& saturdaySunset,
    const ClockConfig& config);

const char* fridayPhaseName(FridayPhase phase);
