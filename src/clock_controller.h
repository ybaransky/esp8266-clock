#pragma once

#include <Arduino.h>
#include <RTClib.h>

struct ClockConfig;
class DisplayManager;
class RtcService;

// Coordinates application-level clock actions. Hardware and feature modules
// remain unchanged for now; this boundary lets callers stop coordinating them
// independently as the architecture is migrated incrementally.
class ClockController {
 public:
  ClockController(DisplayManager& displayManager, RtcService& rtc)
      : displayManager_(displayManager), rtc_(rtc) {}

  void applyConfig(const ClockConfig& config);
  void onSecondBoundary(const DateTime& now);
  void setTime(const DateTime& now);
  void setBrightness(uint8_t brightness);
  void showDemo();
  void showInfo(const char* message, int32_t durationMs);
  void showSplash(const char* message);

 private:
  DisplayManager& displayManager_;
  RtcService& rtc_;
};
