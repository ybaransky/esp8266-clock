#pragma once

#include <Arduino.h>

#include "clock_controller.h"
#include "page_manager.h"

class ClockApplication {
 public:
  void begin();
  void tick(uint32_t nowMs);

 private:
  void processButtonEvents();
  void checkRtcHealth(uint32_t nowMs);
  void logModeOrViewTransition();

  ClockController clockController_;
  PageManager pageManager_;
};
