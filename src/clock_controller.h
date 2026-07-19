#pragma once

#include <Arduino.h>
#include <RTClib.h>

#include "friday_mode.h"
#include "trading_mode.h"

struct ClockConfig;
enum Mode : uint8_t;
class DisplayManager;
class RtcService;

// Coordinates application actions shared by the main loop and web APIs.
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
  Mode activeMode() const;
  bool demoActive() const;

 private:
  DisplayManager& displayManager_;  // Applies view, overlay, and brightness actions.
  RtcService& rtc_;  // Reads and updates the hardware clock.
  FridayModeController fridayMode_;  // Owns Friday schedule state and cache.
  TradingModeController tradingMode_;  // Owns Trading schedule state.
};
