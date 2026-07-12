#include "clock_controller.h"

#include "config.h"
#include "display_manager.h"
#include "friday_mode.h"
#include "rtc_ds3231.h"

// -----------------------------------------------------------------------------
// ClockController
// -----------------------------------------------------------------------------

void ClockController::applyConfig(const ClockConfig& config) {
  displayManager_.applySettings(config);
  fridayModeApplySettings(config);
}

void ClockController::onSecondBoundary(const DateTime& now) {
  // Keep display rendering phase-locked to the accepted RTC SQW edge, then
  // update Friday mode from the same cached wall-clock value.
  displayManager_.notifySecondBoundary();
  fridayModeTick(now, displayManager_);
}

void ClockController::setTime(const DateTime& now) {
  rtc_.setNow(now);
  fridayModeResetSunsetCache();
}

void ClockController::setBrightness(uint8_t brightness) {
  displayManager_.setBrightness(brightness);
}

void ClockController::showDemo() {
  displayManager_.showDemo();
}

void ClockController::showInfo(const char* message, int32_t durationMs) {
  displayManager_.showInfo(message, durationMs);
}

void ClockController::showSplash(const char* message) {
  displayManager_.showSplash(message);
}
