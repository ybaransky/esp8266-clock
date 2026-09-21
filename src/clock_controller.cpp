#include "clock_controller.h"

#include "config.h"
#include "config_validation.h"
#include "datetime_validation.h"
#include "display_manager.h"
#include "rtc_ds3231.h"
#include "sound_player.h"

// -----------------------------------------------------------------------------
// ClockController
// -----------------------------------------------------------------------------

ViewState ClockController::initialView(const ClockConfig& config, const DateTime& now) {
  ViewState view;
  switch (mode_) {
    case kModeFriday:
    case kModeTrading:
      return scheduledMode_.start(now);
    case kModeCountdown:
      view.view = View::kCountdown;
      parseLocalDateTime(config.countdown.end, view.anchor);
      view.formatIndex = config.countdown.format;
      break;
    case kModeCountup:
      view.view = View::kCountup;
      view.anchor = now;
      if (strcmp(config.countup.start, "now") != 0) {
        parseLocalDateTime(config.countup.start, view.anchor);
      }
      view.formatIndex = config.countup.format;
      break;
    case kModeClock:
      view.formatIndex = config.display.clockFmt;
      break;
  }
  return view;
}

void ClockController::applyConfig(const ClockConfig& config) {
  mode_ = config.activeMode;
  sound_.setVolume(config.sound.volumePercent);
  strlcpy(finalSound_, activeSoundName(config.sound, config.sound.final), sizeof(finalSound_));
  sound_.cancelBoundaryAlert();
  scheduledMode_.applySettings(config);
  const DateTime now = rtc_.getNowCached();
  const ViewState view = initialView(config, now);
  countdownEnd_ = view.anchor;
  countdownComplete_ = false;
  displayManager_.applySettings(config, view);
  updateCountdown(now, false);
  const uint32_t nowMs = millis();
  refreshSchedule(now, nowMs - rtc_.msIntoSecond(nowMs));
}

void ClockController::updateCountdown(const DateTime& now, bool announce) {
  if (mode_ != kModeCountdown) return;
  const bool complete = now.unixtime() >= countdownEnd_.unixtime();
  if (complete == countdownComplete_) return;
  countdownComplete_ = complete;
  displayManager_.setCountdownComplete(complete);
  if (complete && announce) sound_.play(finalSound_, millis());
}

void ClockController::refreshSchedule(const DateTime& now, uint32_t secondStartedAtMs) {
  scheduledMode_.tick(now, secondStartedAtMs, displayManager_, sound_);
}

void ClockController::onSecondBoundary(const RtcTick& tick) {
  const bool topOfHour = (tick.now.minute() == 0) && (tick.now.second() == 0);
  displayManager_.notifySecondBoundary(topOfHour);
  if (tick.discontinuity) {
    scheduledMode_.reset();
    sound_.cancelBoundaryAlert();
  }
  refreshSchedule(tick.now, tick.secondStartedAtMs);
  updateCountdown(tick.now, !tick.discontinuity);
}

void ClockController::setTime(const DateTime& now) {
  rtc_.setNow(now);
  sound_.cancelBoundaryAlert();
  scheduledMode_.reset();
  const DateTime actualNow = rtc_.getNowCached();
  const uint32_t nowMs = millis();
  refreshSchedule(actualNow, nowMs - rtc_.msIntoSecond(nowMs));
  updateCountdown(actualNow, false);
  displayManager_.notifySecondBoundary();
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

void ClockController::previewBoundaryAlert(uint16_t frequencyHz,
                                           uint16_t totalDurationSeconds,
                                           uint8_t startingBeatsHz) {
  sound_.previewBoundaryAlert(frequencyHz, totalDurationSeconds,
                              startingBeatsHz, millis());
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

View ClockController::activeView() const {
  return displayManager_.activeView();
}

bool ClockController::demoActive() const {
  return displayManager_.demoActive();
}
