#pragma once

#include <Arduino.h>

#include "clock_controller.h"
#include "config.h"
#include "display.h"
#include "display_manager.h"
#include "page_manager.h"
#include "rtc_ds3231.h"
#include "wifi_connection_manager.h"
#include "web_server.h"

// Owns the firmware services and coordinates their startup and per-loop work.
class ClockApplication {
 public:
  ClockApplication();
  void begin();
  void tick(uint32_t nowMs);

 private:
  void processButtonEvents();
  void checkRtcHealth(uint32_t nowMs);
  void logModeOrViewTransition();

  SegmentDisplay segmentDisplay_;  // Physical three-panel display driver.
  RtcService rtc_;  // RTC access, SQW processing, and cached wall-clock time.
  DisplayManager displayManager_;  // Display view, overlay, and render policy.
  ClockController clockController_;  // Application actions shared with APIs.
  ConfigManager configManager_;  // Persistent clock and WiFi configuration.
  PageManager pageManager_;  // Builds paged button-information overlays.
  WifiConnectionManager wifiConnectionManager_;  // Station/AP network lifecycle.
  WebPortal webPortal_;  // HTTP and captive-portal DNS service.
  uint32_t lastRtcHealthCheckMs_ = 0;  // Last RTC health-poll time.
  bool rtcWasHealthy_ = true;  // Health state used to detect RTC transitions.
  Mode lastLoggedMode_ = kModeClock;  // Mode in the last transition log.
  View lastLoggedView_ = View::kClock;  // View in the last transition log.
};
