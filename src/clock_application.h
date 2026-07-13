#pragma once

#include <Arduino.h>

#include "clock_controller.h"
#include "config.h"
#include "display.h"
#include "display_manager.h"
#include "page_manager.h"
#include "rtc_ds3231.h"
#include "wifi_connection_manager.h"

class ClockApplication {
 public:
  ClockApplication();
  void begin();
  void tick(uint32_t nowMs);

 private:
  void processButtonEvents();
  void checkRtcHealth(uint32_t nowMs);
  void logModeOrViewTransition();

  SegmentDisplay segmentDisplay_;
  RtcService rtc_;
  DisplayManager displayManager_;
  ClockController clockController_;
  ConfigManager configManager_;
  PageManager pageManager_;
  WifiConnectionManager wifiConnectionManager_;
  uint32_t maxTickUs_ = 0;          // Longest tick() this report period.
  uint32_t lastTickReportMs_ = 0;   // Last max-tick log time.
  uint32_t lastRtcHealthCheckMs_ = 0;
  bool rtcWasHealthy_ = true;
  Mode lastLoggedMode_ = kModeClock;
  View lastLoggedView_ = View::kClock;
};
