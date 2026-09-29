#pragma once

#include <Arduino.h>

#include "clock_controller.h"
#include "config.h"
#include "display.h"
#include "display_manager.h"
#include "network_time.h"
#include "page_manager.h"
#include "rtc_ds3231.h"
#include "beep_player.h"
#include "wifi_connection_manager.h"
#include "web_server.h"

// Owns the firmware services and coordinates their startup and per-loop work.
class ClockApplication {
 public:
  ClockApplication();
  void begin();
  void tick(uint32_t nowMs);

 private:
  void initializeRtc();
  // Converts a schema-1 RTC, which held local time, to UTC. Runs once.
  void migrateRtcToUtcIfNeeded();
  void initializeDisplayAndConfig();
  void reportInitialRtcStatus(const RtcStatus& status);
  void processButtonEvents();
  void checkRtcHealth(uint32_t nowMs);
  void logModeOrViewTransition();

  SegmentDisplay m_segmentDisplay;  // Physical three-panel display driver.
  BeepPlayer m_beepPlayer;  // Generated beep output and timing.
  RtcService m_rtc;  // RTC access, SQW processing, and cached wall-clock time.
  DisplayManager m_displayManager;  // Display view, overlay, and render policy.
  ClockController m_clockController;  // Application actions shared with APIs.
  ConfigManager m_configManager;  // Persistent clock and WiFi configuration.
  PageManager m_pageManager;  // Builds paged button-information overlays.
  WifiConnectionManager m_wifiConnectionManager;  // Station/AP network lifecycle.
  NetworkTimeSync m_networkTime;  // Corrects the RTC from NTP in station mode.
  WebPortal m_webPortal;  // HTTP and captive-portal DNS service.
  uint32_t m_lastRtcHealthCheckMs = 0;  // Last RTC health-poll time.
  bool m_rtcWasHealthy = true;  // Health state used to detect RTC transitions.
  Mode m_lastLoggedMode = kModeClock;  // Mode in the last transition log.
  View m_lastLoggedView = View::kClock;  // View in the last transition log.
};
