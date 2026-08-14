#include "clock_controller.h"

#include "config.h"
#include "config_validation.h"
#include "display_manager.h"
#include "rtc_ds3231.h"
#include "sound_player.h"

// -----------------------------------------------------------------------------
// ClockController
// -----------------------------------------------------------------------------

void ClockController::applyConfig(const ClockConfig& config) {
  displayManager_.applySettings(config);
  sound_.setVolume(config.sound.volumePercent);
  strlcpy(finalSound_, activeSoundName(config.sound, config.sound.final),
          sizeof(finalSound_));
  fridayMode_.applySettings(config);
  tradingMode_.applySettings(config);
  fridayMode_.tick(rtc_.getNowCached(), outputs_);
  tradingMode_.tick(rtc_.getNowCached(), outputs_);
  // Installing a fresh countdown view can retire an unconsumed completion from
  // the previous configuration; drop it so the new countdown starts clean.
  displayManager_.consumeCountdownCompleted();
}

void ClockController::tick() {
  if (displayManager_.consumeCountdownCompleted()) {
    sound_.play(finalSound_, millis());
  }
}

void ClockController::onSecondBoundary(const DateTime& now) {
  // Keep display rendering phase-locked to the accepted RTC SQW edge, then
  // update scheduled modes from the same cached wall-clock value.
  displayManager_.notifySecondBoundary();
  fridayMode_.tick(now, outputs_);
  tradingMode_.tick(now, outputs_);
}

void ClockController::setTime(const DateTime& now) {
  rtc_.setNow(now);
  fridayMode_.resetSchedule();
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

bool ClockController::playSound(const char* name) {
  return sound_.play(name, millis());
}

void ClockController::stopSound() {
  sound_.stop();
}

bool ClockController::soundNamesAsJson(JsonArray array, SoundKind kind) {
  return sound_.namesAsJson(array, kind);
}

uint32_t ClockController::soundDurationMs(const char* name) {
  return sound_.durationMs(name);
}

Mode ClockController::activeMode() const {
  return displayManager_.activeMode();
}

View ClockController::activeView() const {
  return displayManager_.activeView();
}

bool ClockController::demoActive() const {
  return displayManager_.demoActive();
}
