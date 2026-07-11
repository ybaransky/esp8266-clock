#pragma once

#include <Arduino.h>
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
  void setBrightness(uint8_t brightness);
  void showDemo();
  void showInfo(const char* message, int32_t durationMs);
  void showSplash(const char* message);
};
