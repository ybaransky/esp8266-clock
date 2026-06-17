#include "display_manager.h"

#include "clock_format.h"
#include "clock_state.h"
#include "display.h"
#include "rtc_ds3231.h"

namespace {

constexpr uint32_t kSecondMs = 1000;
constexpr uint32_t kTenthMs = 100;
constexpr uint32_t kBlinkMs = 500;
constexpr uint32_t kSplashDurationMs = 5000;
constexpr uint32_t kDemoCountdownMs = 5000;
constexpr uint32_t kDemoMessageMs = 5000;
// TM1637 modules need a short quiet period after an overlay ends before
// accepting the base frame reliably on the shared CLK bus.
constexpr uint32_t kOverlayToBaseDelayMs = 10;

DateTime parseDateTime(const char* s) {
  int y = 2000, mo = 1, d = 1, h = 0, mi = 0, sec = 0;
  sscanf(s, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &sec);
  return DateTime(y, mo, d, h, mi, sec);
}

TimeFields deltaToFields(long totalSecs) {
  if (totalSecs < 0) totalSecs = 0;
  TimeFields f = {};
  f.days = totalSecs / 86400;
  totalSecs %= 86400;
  f.hours = totalSecs / 3600;
  totalSecs %= 3600;
  f.minutes = totalSecs / 60;
  f.seconds = totalSecs % 60;
  return f;
}

TimeFields rtcToFields(const DateTime& dt) {
  TimeFields f = {};
  f.year = dt.year();
  f.month = dt.month();
  f.dayOfMonth = dt.day();
  f.hours = dt.hour();
  f.minutes = dt.minute();
  f.seconds = dt.second();
  return f;
}

void messageToBuffers(const char* msg, char r1[8], char r2[8], char r3[8]) {
  const int len = strlen(msg);
  snprintf(r1, 8, "%-4.4s", len > 0 ? msg : "    ");
  snprintf(r2, 8, "%-4.4s", len > 4 ? msg + 4 : "    ");
  snprintf(r3, 8, "%-4.4s", len > 8 ? msg + 8 : "    ");
}

void showBlinkingMessage(const char* msg, bool blinkOn) {
  char r1[8], r2[8], r3[8];
  if (blinkOn) {
    messageToBuffers(msg, r1, r2, r3);
  } else {
    snprintf(r1, 8, "    ");
    snprintf(r2, 8, "    ");
    snprintf(r3, 8, "    ");
  }
  segmentDisplay.showPanels(r1, r2, r3);
}

}  // namespace

void DisplayManager::begin(const ClockConfig& config) {
  applySettings(config);
}

void DisplayManager::applySettings(const ClockConfig& config) {
  settings_ = config;
  lastBaseTickMs_ = 0;
  colonVisible_ = true;
  colonMs_ = 0;

  updateCountupOrigin(config);
  segmentDisplay.setBrightness(config.brightness);
  setBaseMode(baseModeFor(config.activeMode));

  if (overlayMode_ == OverlayDisplayMode::kSplash) {
    finishOverlay(millis());
  }
}

void DisplayManager::tick(uint32_t nowMs) {
  if (overlayMode_ == OverlayDisplayMode::kNone) {
    if (baseHandoffDueMs_ != 0) {
      if (static_cast<long>(nowMs - baseHandoffDueMs_) < 0) {
        return;
      }
      baseHandoffDueMs_ = 0;
      tickBase(nowMs, true);
      return;
    }

    tickBase(nowMs);
    return;
  }

  if (tickActiveOverlay(nowMs)) {
    finishOverlay(nowMs);
  }
}

void DisplayManager::showSplash(const char* message) {
  startOverlay(OverlayDisplayMode::kSplash, message, FOREVER, millis());
}

void DisplayManager::showDemo() {
  startOverlay(OverlayDisplayMode::kDemo, "", FOREVER, millis());
}

void DisplayManager::showInfo(const char* message, int32_t durationMs) {
  startOverlay(OverlayDisplayMode::kInfo, message, durationMs, millis());
}

void DisplayManager::clearInfo() {
  if (overlayMode_ == OverlayDisplayMode::kInfo) {
    finishOverlay(millis());
  }
}

const char* DisplayManager::baseModeName() const {
  return baseModeName(baseMode_);
}

const char* DisplayManager::overlayModeName() const {
  return overlayModeName(overlayMode_);
}

BaseDisplayMode DisplayManager::baseModeFor(BaseMode mode) const {
  switch (mode) {
    case kBaseCountup:
      return BaseDisplayMode::kCountup;
    case kBaseClock:
      return BaseDisplayMode::kClock;
    case kBaseCountdown:
      return BaseDisplayMode::kCountdown;
  }
  return BaseDisplayMode::kCountdown;
}

const char* DisplayManager::baseModeName(BaseDisplayMode mode) const {
  switch (mode) {
    case BaseDisplayMode::kClock:
      return "clock";
    case BaseDisplayMode::kCountdown:
      return "countdown";
    case BaseDisplayMode::kCountup:
      return "countup";
  }
  return "?";
}

const char* DisplayManager::overlayModeName(OverlayDisplayMode mode) const {
  switch (mode) {
    case OverlayDisplayMode::kNone:
      return "none";
    case OverlayDisplayMode::kSplash:
      return "splash";
    case OverlayDisplayMode::kDemo:
      return "demo";
    case OverlayDisplayMode::kInfo:
      return "info";
  }
  return "?";
}

void DisplayManager::setBaseMode(BaseDisplayMode next) {
  if (next == baseMode_) {
    return;
  }

  Serial.printf("[MODE] %s  base[%s->%s] overlay[%s]\n",
                rtcGetCurrentTimeString().c_str(),
                baseModeName(baseMode_), baseModeName(next),
                overlayModeName(overlayMode_));
  baseMode_ = next;
}

void DisplayManager::startOverlay(OverlayDisplayMode overlay, const char* message,
                                  int32_t durationMs, uint32_t nowMs) {
  Serial.printf("[MODE] %s  base[%s] overlay[%s->%s]\n",
                rtcGetCurrentTimeString().c_str(),
                baseModeName(baseMode_),
                overlayModeName(overlayMode_), overlayModeName(overlay));
  baseHandoffDueMs_ = 0;
  overlayMode_ = overlay;
  modeStartMs_ = nowMs;
  infoDurationMs_ = durationMs;
  blinkOn_ = true;
  blinkMs_ = nowMs;
  strncpy(overlayMessage_, message, sizeof(overlayMessage_) - 1);
  overlayMessage_[sizeof(overlayMessage_) - 1] = '\0';
  segmentDisplay.blank();
}

void DisplayManager::finishOverlay(uint32_t nowMs) {
  if (overlayMode_ == OverlayDisplayMode::kNone) {
    return;
  }

  Serial.printf("[MODE] %s  base[%s] overlay[%s->%s]\n",
                rtcGetCurrentTimeString().c_str(),
                baseModeName(baseMode_),
                overlayModeName(overlayMode_),
                overlayModeName(OverlayDisplayMode::kNone));
  overlayMode_ = OverlayDisplayMode::kNone;
  lastBaseTickMs_ = 0;

  baseHandoffDueMs_ = nowMs + kOverlayToBaseDelayMs;
}

void DisplayManager::updateCountupOrigin(const ClockConfig& config) {
  countupOrigin_ = (strncmp(config.countupDatetime, "now", 3) == 0)
      ? rtcGetNow()
      : parseDateTime(config.countupDatetime);
}

uint32_t DisplayManager::baseRefreshInterval() const {
  const bool needsTenths =
      (baseMode_ == BaseDisplayMode::kCountdown &&
       countdownHasTenths(settings_.countdownFmt)) ||
      (baseMode_ == BaseDisplayMode::kCountup &&
       countupHasTenths(settings_.countupFmt)) ||
      (baseMode_ == BaseDisplayMode::kClock &&
       clockHasTenths(settings_.clockFmt));
  return needsTenths ? kTenthMs : kSecondMs;
}

bool DisplayManager::baseElapsed(uint32_t nowMs, bool force) {
  if (force) {
    lastBaseTickMs_ = nowMs;
    return true;
  }

  if (static_cast<long>(nowMs - lastBaseTickMs_) <
      static_cast<long>(baseRefreshInterval())) {
    return false;
  }
  lastBaseTickMs_ = nowMs;
  return true;
}

void DisplayManager::tickBase(uint32_t nowMs, bool force) {
  switch (baseMode_) {
    case BaseDisplayMode::kClock:
      tickClock(nowMs, force);
      break;
    case BaseDisplayMode::kCountdown:
      tickCountdown(nowMs, force);
      break;
    case BaseDisplayMode::kCountup:
      tickCountup(nowMs, force);
      break;
  }
}

bool DisplayManager::tickActiveOverlay(uint32_t nowMs) {
  switch (overlayMode_) {
    case OverlayDisplayMode::kNone:
      return false;
    case OverlayDisplayMode::kSplash:
      return tickSplash(nowMs);
    case OverlayDisplayMode::kDemo:
      return tickDemo(nowMs);
    case OverlayDisplayMode::kInfo:
      return tickInfo(nowMs);
  }
  return false;
}

void DisplayManager::tickClock(uint32_t nowMs, bool force) {
  if (static_cast<long>(nowMs - colonMs_) >= static_cast<long>(kBlinkMs)) {
    colonMs_ = nowMs;
    colonVisible_ = !colonVisible_;
  }
  if (!baseElapsed(nowMs, force)) return;

  TimeFields f = rtcToFields(rtcGetNow());
  if (clockHasTenths(settings_.clockFmt)) {
    f.tenths = (nowMs % kSecondMs) / kTenthMs;
  }

  char r1[8], r2[8], r3[8];
  renderClock(settings_.clockFmt, f, r1, r2, r3, colonVisible_);
  segmentDisplay.showPanels(r1, r2, r3);
}

void DisplayManager::tickCountdown(uint32_t nowMs, bool force) {
  if (!baseElapsed(nowMs, force)) return;

  const DateTime now = rtcGetNow();
  const DateTime target = parseDateTime(settings_.countdownDatetime);
  const long secs =
      static_cast<long>(target.unixtime()) - static_cast<long>(now.unixtime());

  TimeFields f = deltaToFields(secs < 0 ? 0 : secs);
  if (countdownHasTenths(settings_.countdownFmt)) {
    f.tenths = (secs > 0) ? (10 - (nowMs % kSecondMs) / kTenthMs) % 10 : 0;
  }

  char r1[8], r2[8], r3[8];
  renderCountdown(settings_.countdownFmt, f, r1, r2, r3);
  segmentDisplay.showPanels(r1, r2, r3);
}

void DisplayManager::tickCountup(uint32_t nowMs, bool force) {
  if (!baseElapsed(nowMs, force)) return;

  const DateTime now = rtcGetNow();
  const long secs =
      static_cast<long>(now.unixtime()) - static_cast<long>(countupOrigin_.unixtime());

  TimeFields f = deltaToFields(secs < 0 ? 0 : secs);
  if (countupHasTenths(settings_.countupFmt)) {
    f.tenths = (nowMs % kSecondMs) / kTenthMs;
  }

  char r1[8], r2[8], r3[8];
  renderCountup(settings_.countupFmt, f, r1, r2, r3);
  segmentDisplay.showPanels(r1, r2, r3);
}

bool DisplayManager::tickSplash(uint32_t nowMs) {
  if (static_cast<long>(nowMs - modeStartMs_) >=
      static_cast<long>(kSplashDurationMs)) {
    return true;
  }

  char r1[8], r2[8], r3[8];
  messageToBuffers(overlayMessage_, r1, r2, r3);
  segmentDisplay.showPanels(r1, r2, r3);
  return false;
}

bool DisplayManager::tickInfo(uint32_t nowMs) {
  if (infoDurationMs_ != FOREVER &&
      static_cast<long>(nowMs - modeStartMs_) >= infoDurationMs_) {
    return true;
  }

  if (static_cast<long>(nowMs - blinkMs_) >= static_cast<long>(kBlinkMs)) {
    blinkMs_ = nowMs;
    blinkOn_ = !blinkOn_;
  }
  showBlinkingMessage(overlayMessage_, blinkOn_);
  return false;
}

bool DisplayManager::tickDemo(uint32_t nowMs) {
  const uint32_t elapsed = nowMs - modeStartMs_;

  if (elapsed < kDemoCountdownMs) {
    const uint32_t remaining = kDemoCountdownMs - elapsed;
    const uint8_t whole =
        static_cast<uint8_t>(min<uint32_t>(9, remaining / kSecondMs));
    const uint8_t tenths = static_cast<uint8_t>(
        min<uint32_t>(9, (remaining % kSecondMs) / kTenthMs));
    char r3[8];
    snprintf(r3, sizeof(r3), "%u.%u", whole, tenths);
    segmentDisplay.showPanels("    ", "    ", r3);
    return false;
  }

  if (elapsed < kDemoCountdownMs + kDemoMessageMs) {
    if (static_cast<long>(nowMs - blinkMs_) >= static_cast<long>(kBlinkMs)) {
      blinkMs_ = nowMs;
      blinkOn_ = !blinkOn_;
    }
    showBlinkingMessage(settings_.finalMessage, blinkOn_);
    return false;
  }

  return true;
}

DisplayManager displayManager;

void clockApplySettings(const ClockConfig& cfg) {
  displayManager.applySettings(cfg);
}

void clockTriggerDemo() {
  displayManager.showDemo();
}

void clockShowInfo(const char* msg, int32_t durationMs) {
  displayManager.showInfo(msg, durationMs);
}

void clockClearInfo() {
  displayManager.clearInfo();
}
