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

void ClockController::setBrightness(uint8_t brightness) {
  displayManager.setBrightness(brightness);
}

void ClockController::showDemo() {
  displayManager.showDemo();
}

void ClockController::showInfo(const char* message, int32_t durationMs) {
  displayManager.showInfo(message, durationMs);
}

void ClockController::showSplash(const char* message) {
  displayManager.showSplash(message);
}
