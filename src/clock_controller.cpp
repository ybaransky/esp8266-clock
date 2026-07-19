#include "clock_controller.h"

#include "config.h"
#include "display_manager.h"
#include "rtc_ds3231.h"

// -----------------------------------------------------------------------------
// ClockController
// -----------------------------------------------------------------------------

void ClockController::applyConfig(const ClockConfig& config) {
  displayManager_.applySettings(config);
  fridayMode_.applySettings(config);
  tradingMode_.applySettings(config);
  fridayMode_.tick(rtc_.getNowCached(), displayManager_);
  tradingMode_.tick(rtc_.getNowCached(), displayManager_);
}

void ClockController::onSecondBoundary(const DateTime& now) {
  // Keep display rendering phase-locked to the accepted RTC SQW edge, then
  // update scheduled modes from the same cached wall-clock value.
  displayManager_.notifySecondBoundary();
  fridayMode_.tick(now, displayManager_);
  tradingMode_.tick(now, displayManager_);
}

void ClockController::setTime(const DateTime& now) {
  rtc_.setNow(now);
  fridayMode_.resetSunsetCache();
  tradingMode_.resetSchedule();
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

Mode ClockController::activeMode() const {
  return displayManager_.activeMode();
}

bool ClockController::demoActive() const {
  return displayManager_.demoActive();
}
