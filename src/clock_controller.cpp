#include "clock_controller.h"

#include "config.h"
#include "config_validation.h"
#include "datetime_validation.h"
#include "display_manager.h"
#include "rtc_ds3231.h"
#include "beep_player.h"

// -----------------------------------------------------------------------------
// ClockController
// -----------------------------------------------------------------------------

// Stored datetimes are local wall-clock text (what the user typed); anchors
// are UTC so the displayed duration is real elapsed time across a DST change.
uint32_t ClockController::localTextToUtc(const char* text) const {
  DateTime local;
  parseLocalDateTime(text, local);
  return utcFromLocal(m_rtc.timeZone(), local.unixtime());
}

ViewState ClockController::initialView(const ClockConfig& config, const DateTime& now,
                                       uint32_t nowUtc) {
  ViewState view;
  switch (m_mode) {
    case kModeFriday:
    case kModeTrading:
      return m_scheduledMode.start(now, nowUtc);
    case kModeCountdown:
      view.view = View::kCountdown;
      view.anchorUtc = localTextToUtc(config.countdown.end);
      view.formatIndex = config.countdown.format;
      break;
    case kModeCountup:
      view.view = View::kCountup;
      // The sentinel only survives to here on a device that has never saved
      // its config: resolveCountupStart() replaces it on the first save, which
      // is what keeps the origin stable across later saves and reboots.
      view.anchorUtc = nowUtc;
      if (strcmp(config.countup.start, kCountupStartNow) != 0) {
        view.anchorUtc = localTextToUtc(config.countup.start);
      }
      view.formatIndex = config.countup.format;
      break;
    case kModeClock:
      view.formatIndex = config.display.clockFormat;
      break;
  }
  return view;
}

void ClockController::applyConfig(const ClockConfig& config) {
  // First, so every local time below is derived through the new rule.
  m_rtc.setTimeZone(timeZoneFromConfig(config.timezone));
  m_mode = config.activeMode;
  m_sound.setVolume(config.sound.volumePercent);
  m_finalBeep = config.sound.enabled && config.sound.finalBeep;
  m_sound.cancelBoundaryAlert();
  m_scheduledMode.applySettings(config);
  const DateTime now = m_rtc.getNowCached();
  const uint32_t nowUtc = m_rtc.getUtcCached();
  const ViewState view = initialView(config, now, nowUtc);
  m_countdownEndUtc = view.anchorUtc;
  m_countdownComplete = false;
  m_displayManager.applySettings(config, view);
  updateCountdown(nowUtc, false);
  const uint32_t nowMs = millis();
  refreshSchedule(now, nowUtc, nowMs - m_rtc.msIntoSecond(nowMs));
}

void ClockController::updateCountdown(uint32_t nowUtc, bool announce) {
  if (m_mode != kModeCountdown) return;
  const bool complete = nowUtc >= m_countdownEndUtc;
  if (complete == m_countdownComplete) return;
  m_countdownComplete = complete;
  m_displayManager.setCountdownComplete(complete);
  if (complete && announce && m_finalBeep) m_sound.beep(880, millis());
}

void ClockController::refreshSchedule(const DateTime& now, uint32_t nowUtc,
                                      uint32_t secondStartedAtMs) {
  m_scheduledMode.tick(now, nowUtc, secondStartedAtMs, m_displayManager, m_sound);
}

void ClockController::onSecondBoundary(const RtcTick& tick) {
  const bool topOfHour = (tick.now.minute() == 0) && (tick.now.second() == 0);
  m_displayManager.notifySecondBoundary(topOfHour);
  if (tick.discontinuity) {
    m_scheduledMode.reset();
    m_sound.cancelBoundaryAlert();
  }
  refreshSchedule(tick.now, tick.utc, tick.secondStartedAtMs);
  updateCountdown(tick.utc, !tick.discontinuity);
}

void ClockController::setTime(uint32_t utc, const char* reason) {
  m_rtc.setUtc(utc, reason);
  m_sound.cancelBoundaryAlert();
  m_scheduledMode.reset();
  const DateTime actualNow = m_rtc.getNowCached();
  const uint32_t actualUtc = m_rtc.getUtcCached();
  const uint32_t nowMs = millis();
  refreshSchedule(actualNow, actualUtc, nowMs - m_rtc.msIntoSecond(nowMs));
  updateCountdown(actualUtc, false);
  m_displayManager.notifySecondBoundary();
}

void ClockController::setBrightness(uint8_t brightness) {
  m_displayManager.setBrightness(brightness);
}

void ClockController::showDemo() {
  m_displayManager.showDemo();
}

void ClockController::showInfo(const char* message, int32_t durationMs) {
  m_displayManager.showInfo(message, durationMs);
}

void ClockController::showSplash(const char* message) {
  m_displayManager.showSplash(message);
}

View ClockController::activeView() const {
  return m_displayManager.activeView();
}

bool ClockController::demoActive() const {
  return m_displayManager.demoActive();
}
