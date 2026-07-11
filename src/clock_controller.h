#pragma once

#include <RTClib.h>

struct ClockConfig;

// Coordinates application-level clock actions. Hardware and feature modules
// remain unchanged for now; this boundary lets callers stop coordinating them
// independently as the architecture is migrated incrementally.
class ClockController {
 public:
  void applyConfig(const ClockConfig& config);
  void onSecondBoundary(const DateTime& now);
  void setTime(const DateTime& now);
};
