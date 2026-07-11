#include "clock_controller.h"

#include "clock_state.h"
#include "config.h"
#include "display_manager.h"
#include "friday_mode.h"
#include "rtc_ds3231.h"

void ClockController::applyConfig(const ClockConfig& config) {
  clockApplySettings(config);
}

void ClockController::onSecondBoundary(const DateTime& now) {
  // Keep display rendering phase-locked to the accepted RTC SQW edge, then
  // update Friday mode from the same cached wall-clock value.
  displayManager.notifySecondBoundary();
  fridayModeTick(now);
}

void ClockController::setTime(const DateTime& now) {
  rtcSetNow(now);
  fridayModeResetSunsetCache();
}
