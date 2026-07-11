#pragma once

#include <Arduino.h>

#include "clock_controller.h"
#include "config.h"
#include "display.h"
#include "display_manager.h"
#include "page_manager.h"
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
  DisplayManager displayManager_;
  ClockController clockController_;
  ConfigManager configManager_;
  PageManager pageManager_;
  WifiConnectionManager wifiConnectionManager_;
};
