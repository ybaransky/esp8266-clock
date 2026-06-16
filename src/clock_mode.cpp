#include "clock_mode.h"

#include "clock_format.h"
#include "display.h"
#include "rtc_ds3231.h"
#include <Arduino.h>

// ── Timing constants ──────────────────────────────────────────────────────────
static constexpr uint32_t kSecondMs         = 1000;
static constexpr uint32_t kTenthMs          = 100;
static constexpr uint32_t kBlinkMs          = 500;
static constexpr uint32_t kSplashDurationMs = 5000;
static constexpr uint32_t kDemoCountdownMs  = 5000;
static constexpr uint32_t kDemoMessageMs    = 5000;

// ── File-scope helpers ────────────────────────────────────────────────────────

static DateTime parseDateTime(const char* s) {
  int y = 2000, mo = 1, d = 1, h = 0, mi = 0, sec = 0;
  sscanf(s, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &sec);
  return DateTime(y, mo, d, h, mi, sec);
}

static TimeFields deltaToFields(long totalSecs) {
  if (totalSecs < 0) totalSecs = 0;
  TimeFields f = {};
  f.days    = totalSecs / 86400; totalSecs %= 86400;
  f.hours   = totalSecs / 3600;  totalSecs %= 3600;
  f.minutes = totalSecs / 60;
  f.seconds = totalSecs % 60;
  return f;
}

static TimeFields rtcToFields(const DateTime& dt) {
  TimeFields f = {};
  f.year       = dt.year();
  f.month      = dt.month();
  f.dayOfMonth = dt.day();
  f.hours      = dt.hour();
  f.minutes    = dt.minute();
  f.seconds    = dt.second();
  return f;
}

static void messageToBuffers(const char* msg, char r1[8], char r2[8], char r3[8]) {
  const int len = strlen(msg);
  snprintf(r1, 8, "%-4.4s", len > 0 ? msg     : "    ");
  snprintf(r2, 8, "%-4.4s", len > 4 ? msg + 4 : "    ");
  snprintf(r3, 8, "%-4.4s", len > 8 ? msg + 8 : "    ");
}

// ── Static helper ─────────────────────────────────────────────────────────────

ClockModeEngine::DisplayMode ClockModeEngine::baseModeToDisplay(BaseMode m) {
  switch (m) {
    case kBaseCountup: return DisplayMode::kCountup;
    case kBaseClock:   return DisplayMode::kClock;
    default:           return DisplayMode::kCountdown;
  }
}

// ── Public API ────────────────────────────────────────────────────────────────

void ClockModeEngine::begin(const ClockConfig& s) {
  applySettings(s);
  if (s.splashMessage[0] != '\0') {
    enterTransientMode(DisplayMode::kSplash, s.splashMessage);
  }
}

void ClockModeEngine::applySettings(const ClockConfig& s) {
  settings_       = s;
  lastBaseTickMs_ = 0;
  colonVisible_   = true;
  colonMs_        = 0;

  countupOrigin_ = (strncmp(s.countupDatetime, "now", 3) == 0)
      ? rtcGetNow()
      : parseDateTime(s.countupDatetime);

  segmentDisplay.setBrightness(s.brightness);

  // Always update previousMode_ so transient modes return to the new setting.
  previousMode_ = baseModeToDisplay(s.activeMode);

  // If currently in a base mode, follow the new setting immediately.
  if (currentMode_ <= DisplayMode::kClock) {
    currentMode_ = previousMode_;
  }
}

void ClockModeEngine::triggerDemo() {
  enterTransientMode(DisplayMode::kDemo, "");
}

void ClockModeEngine::showInfo(const char* message, int32_t durationMs) {
  enterTransientMode(DisplayMode::kInfo, message, durationMs);
}

void ClockModeEngine::clearInfo() {
  if (currentMode_ == DisplayMode::kInfo) restoreMode();
}

void ClockModeEngine::tick(uint32_t nowMs) {
  switch (currentMode_) {
    case DisplayMode::kCountdown: tickCountdown(nowMs); return;
    case DisplayMode::kCountup:   tickCountup(nowMs);   return;
    case DisplayMode::kClock:     tickClock(nowMs);     return;
    case DisplayMode::kSplash:    tickSplash(nowMs);    return;
    case DisplayMode::kDemo:      tickDemo(nowMs);      return;
    case DisplayMode::kInfo:      tickInfo(nowMs);      return;
  }
}

// ── Transition helpers ────────────────────────────────────────────────────────

void ClockModeEngine::enterTransientMode(DisplayMode m, const char* msg, int32_t durationMs) {
  previousMode_   = (currentMode_ <= DisplayMode::kClock) ? currentMode_ : previousMode_;
  currentMode_    = m;
  modeStartMs_    = millis();
  infoDurationMs_ = durationMs;
  blinkOn_        = true;
  blinkMs_        = millis();
  strncpy(transientMsg_, msg, sizeof(transientMsg_) - 1);
  transientMsg_[sizeof(transientMsg_) - 1] = '\0';
  segmentDisplay.blank();
}

void ClockModeEngine::restoreMode() {
  currentMode_    = previousMode_;
  lastBaseTickMs_ = 0;  // force immediate base render on next tick
  segmentDisplay.blank();
}

// ── Blink helpers ─────────────────────────────────────────────────────────────

void ClockModeEngine::updateBlink(uint32_t nowMs) {
  if (static_cast<long>(nowMs - blinkMs_) < static_cast<long>(kBlinkMs)) return;
  blinkMs_ = nowMs;
  blinkOn_ = !blinkOn_;
}

// Uses showPanels so the display cache suppresses redundant TM1637 writes.
void ClockModeEngine::showBlinkingMessage(const char* msg) {
  char r1[8], r2[8], r3[8];
  if (blinkOn_) {
    messageToBuffers(msg, r1, r2, r3);
  } else {
    snprintf(r1, 8, "    "); snprintf(r2, 8, "    "); snprintf(r3, 8, "    ");
  }
  segmentDisplay.showPanels(r1, r2, r3);
}

// ── Transient tick handlers ───────────────────────────────────────────────────

void ClockModeEngine::tickSplash(uint32_t nowMs) {
  if (static_cast<long>(nowMs - modeStartMs_) >= static_cast<long>(kSplashDurationMs)) {
    restoreMode();
    return;
  }
  char r1[8], r2[8], r3[8];
  messageToBuffers(transientMsg_, r1, r2, r3);
  segmentDisplay.showPanels(r1, r2, r3);
}

void ClockModeEngine::tickInfo(uint32_t nowMs) {
  if (infoDurationMs_ > 0 &&
      static_cast<long>(nowMs - modeStartMs_) >= infoDurationMs_) {
    restoreMode();
    return;
  }
  updateBlink(nowMs);
  showBlinkingMessage(transientMsg_);
}

void ClockModeEngine::tickDemo(uint32_t nowMs) {
  const uint32_t elapsed = nowMs - modeStartMs_;

  if (elapsed < kDemoCountdownMs) {
    // Phase 1: count down from 5.0 with tenths on display 3 only.
    const uint32_t remaining = kDemoCountdownMs - elapsed;
    const uint8_t  whole     = static_cast<uint8_t>(min<uint32_t>(9, remaining / kSecondMs));
    const uint8_t  tenths    = static_cast<uint8_t>(min<uint32_t>(9, (remaining % kSecondMs) / kTenthMs));
    char r3[8];
    snprintf(r3, sizeof(r3), "%u.%u", whole, tenths);
    segmentDisplay.showPanels("    ", "    ", r3);
  } else if (elapsed < kDemoCountdownMs + kDemoMessageMs) {
    // Phase 2: blink finalMessage across all three panels.
    updateBlink(nowMs);
    showBlinkingMessage(settings_.finalMessage);
  } else {
    restoreMode();
  }
}

// ── Base-mode tick handlers ───────────────────────────────────────────────────

uint32_t ClockModeEngine::baseRefreshInterval() const {
  const bool needsTenths =
      (currentMode_ == DisplayMode::kCountdown && countdownHasTenths(settings_.countdownFmt)) ||
      (currentMode_ == DisplayMode::kCountup   && countupHasTenths(settings_.countupFmt))     ||
      (currentMode_ == DisplayMode::kClock     && clockHasTenths(settings_.clockFmt));
  return needsTenths ? kTenthMs : kSecondMs;
}

bool ClockModeEngine::baseElapsed(uint32_t nowMs) {
  if (static_cast<long>(nowMs - lastBaseTickMs_) < static_cast<long>(baseRefreshInterval())) return false;
  lastBaseTickMs_ = nowMs;
  return true;
}

void ClockModeEngine::tickCountdown(uint32_t nowMs) {
  if (!baseElapsed(nowMs)) return;

  const DateTime now    = rtcGetNow();
  const DateTime target = parseDateTime(settings_.countdownDatetime);
  const long secs       = static_cast<long>(target.unixtime()) - static_cast<long>(now.unixtime());

  TimeFields f = deltaToFields(secs < 0 ? 0 : secs);
  if (countdownHasTenths(settings_.countdownFmt))
    f.tenths = (secs > 0) ? (10 - (nowMs % kSecondMs) / kTenthMs) % 10 : 0;

  char r1[8], r2[8], r3[8];
  renderCountdown(settings_.countdownFmt, f, r1, r2, r3);
  segmentDisplay.showPanels(r1, r2, r3);
}

void ClockModeEngine::tickCountup(uint32_t nowMs) {
  if (!baseElapsed(nowMs)) return;

  const DateTime now = rtcGetNow();
  const long secs    = static_cast<long>(now.unixtime()) - static_cast<long>(countupOrigin_.unixtime());

  TimeFields f = deltaToFields(secs < 0 ? 0 : secs);
  if (countupHasTenths(settings_.countupFmt))
    f.tenths = (nowMs % kSecondMs) / kTenthMs;

  char r1[8], r2[8], r3[8];
  renderCountup(settings_.countupFmt, f, r1, r2, r3);
  segmentDisplay.showPanels(r1, r2, r3);
}

void ClockModeEngine::tickClock(uint32_t nowMs) {
  if (static_cast<long>(nowMs - colonMs_) >= static_cast<long>(kBlinkMs)) {
    colonMs_      = nowMs;
    colonVisible_ = !colonVisible_;
  }
  if (!baseElapsed(nowMs)) return;

  TimeFields f = rtcToFields(rtcGetNow());
  if (clockHasTenths(settings_.clockFmt))
    f.tenths = (nowMs % kSecondMs) / kTenthMs;

  char r1[8], r2[8], r3[8];
  renderClock(settings_.clockFmt, f, r1, r2, r3, colonVisible_);
  segmentDisplay.showPanels(r1, r2, r3);
}

// ── Singleton ─────────────────────────────────────────────────────────────────
ClockModeEngine clockModeEngine;
