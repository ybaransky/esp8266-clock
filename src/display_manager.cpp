#include "display_manager.h"

#include "clock_format.h"
#include "clock_state.h"
#include "display.h"
#include "log.h"
#include "rtc_ds3231.h"

namespace {

constexpr uint32_t kSecondMs = 1000;
constexpr uint32_t kTenthMs = 100;
constexpr uint32_t kBlinkMs = 500;
constexpr uint32_t kSplashDurationMs = 5000;
constexpr uint32_t kDemoCountdownMs = 5000;
constexpr uint32_t kDemoMessageMs = 5000;
constexpr uint8_t kDemoCountdownFormat = 0xFF;

DateTime parseDateTime(const char* s) {
  int y = 2000, mo = 1, d = 1, h = 0, mi = 0, sec = 0;
  sscanf(s, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &sec);
  return DateTime(y, mo, d, h, mi, sec);
}

TimeFields deltaToFields(long totalSecs) {
  if (totalSecs < 0) totalSecs = 0;
  TimeFields fields = {};
  fields.days = totalSecs / 86400;
  totalSecs %= 86400;
  fields.hours = totalSecs / 3600;
  totalSecs %= 3600;
  fields.minutes = totalSecs / 60;
  fields.seconds = totalSecs % 60;
  return fields;
}

TimeFields rtcToFields(const DateTime& dt) {
  TimeFields fields = {};
  fields.year = dt.year();
  fields.month = dt.month();
  fields.dayOfMonth = dt.day();
  fields.hours = dt.hour();
  fields.minutes = dt.minute();
  fields.seconds = dt.second();
  return fields;
}

void copyMessage(char destination[64], const char* source) {
  strncpy(destination, source, 63);
  destination[63] = '\0';
}

void messageToBuffers(const char* msg, char r1[8], char r2[8], char r3[8]) {
  const int len = strlen(msg);
  snprintf(r1, 8, "%-4.4s", len > 0 ? msg : "    ");
  snprintf(r2, 8, "%-4.4s", len > 4 ? msg + 4 : "    ");
  snprintf(r3, 8, "%-4.4s", len > 8 ? msg + 8 : "    ");
}

void blankBuffers(char r1[8], char r2[8], char r3[8]) {
  snprintf(r1, 8, "    ");
  snprintf(r2, 8, "    ");
  snprintf(r3, 8, "    ");
}

}  // namespace

void DisplayManager::begin(const ClockConfig& config) {
  applySettings(config);
}

void DisplayManager::applySettings(const ClockConfig& config) {
  settings_ = config;
  colonVisible_ = true;
  colonMs_ = 0;
  blinkOn_ = true;
  blinkMs_ = millis();
  lastRenderMs_ = 0;

  updateCountupOrigin(config);
  defaultState_ = stateForConfiguredMode(config.activeMode);
  segmentDisplay.setBrightness(config.brightness);
  installDefaultState(millis());
}

void DisplayManager::tick(uint32_t nowMs) {
  if (transitionExpired(nowMs)) {
    finishTemporaryState(nowMs);
  }

  renderCurrentState(nowMs);
}

void DisplayManager::showSplash(const char* message) {
  DisplayState state;
  state.behavior = DisplayBehavior::kMessage;
  state.blink = false;
  state.payload.formatIndex = 0;
  copyMessage(state.payload.message, message);

  installState(state, {true, millis() + kSplashDurationMs}, millis(), true);
}

void DisplayManager::showDemo() {
  DisplayState state;
  state.behavior = DisplayBehavior::kCountdown;
  state.blink = false;
  state.payload.formatIndex = kDemoCountdownFormat;
  state.payload.endTime = rtcGetNow() + TimeSpan(0, 0, 0, 5);

  demoCountdownActive_ = true;
  installState(state, {true, millis() + kDemoCountdownMs}, millis(), true);
}

void DisplayManager::showInfo(const char* message, int32_t durationMs) {
  DisplayState state;
  state.behavior = DisplayBehavior::kMessage;
  state.blink = true;
  state.payload.formatIndex = 0;
  copyMessage(state.payload.message, message);

  const bool expires = durationMs != FOREVER;
  const uint32_t expiresAt = expires ? millis() + static_cast<uint32_t>(durationMs) : 0;
  installState(state, {expires, expiresAt}, millis(), true);
}

void DisplayManager::clearInfo() {
  if (hasPreviousState_ && currentState_.behavior == DisplayBehavior::kMessage) {
    finishTemporaryState(millis());
  }
}

const char* DisplayManager::defaultStateName() const {
  return behaviorName(defaultState_.behavior);
}

const char* DisplayManager::currentStateName() const {
  return behaviorName(currentState_.behavior);
}

DisplayState DisplayManager::stateForConfiguredMode(PersistentMode mode) const {
  DisplayState state;
  state.blink = false;

  switch (mode) {
    case kPersistentCountup:
      state.behavior = DisplayBehavior::kCountup;
      state.payload.startTime = countupOrigin_;
      state.payload.formatIndex = settings_.countupFmt;
      break;
    case kPersistentClock:
      state.behavior = DisplayBehavior::kClock;
      state.payload.formatIndex = settings_.clockFmt;
      break;
    case kPersistentCountdown:
      state.behavior = DisplayBehavior::kCountdown;
      state.payload.endTime = parseDateTime(settings_.countdownDatetime);
      state.payload.formatIndex = settings_.countdownFmt;
      break;
  }

  return state;
}

const char* DisplayManager::behaviorName(DisplayBehavior behavior) const {
  switch (behavior) {
    case DisplayBehavior::kClock:
      return "clock";
    case DisplayBehavior::kCountdown:
      return "countdown";
    case DisplayBehavior::kCountup:
      return "countup";
    case DisplayBehavior::kMessage:
      return "message";
  }
  return "?";
}

void DisplayManager::logStateTransition(const DisplayState& from,
                                        const DisplayState& to,
                                        const char* reason) const {
  LOG_PRINTF("state transition: %s -> %s (%s)\n",
             behaviorName(from.behavior),
             behaviorName(to.behavior),
             reason);
}

void DisplayManager::installState(const DisplayState& state,
                                  const DisplayTransition& transition,
                                  uint32_t nowMs,
                                  bool rememberPrevious) {
  const DisplayState oldState = currentState_;

  if (rememberPrevious) {
    previousState_ = currentState_;
    hasPreviousState_ = true;
  }

  currentState_ = state;
  currentTransition_ = transition;
  lastRenderMs_ = 0;
  blinkOn_ = true;
  blinkMs_ = nowMs;
  segmentDisplay.blank();

  logStateTransition(oldState, currentState_,
                     rememberPrevious ? "temporary state" : "state install");
}

void DisplayManager::installDefaultState(uint32_t nowMs, bool forceRender) {
  demoCountdownActive_ = false;
  hasPreviousState_ = false;
  installState(defaultState_, {false, 0}, nowMs, false);
  if (forceRender) {
    renderCurrentState(nowMs, true);
  }
}

void DisplayManager::finishTemporaryState(uint32_t nowMs) {
  if (demoCountdownActive_) {
    startDemoMessageState(nowMs);
    return;
  }

  restorePreviousState(nowMs);
}

void DisplayManager::restorePreviousState(uint32_t nowMs) {
  if (hasPreviousState_) {
    const DisplayState oldState = currentState_;
    currentState_ = previousState_;
    currentTransition_ = {false, 0};
    hasPreviousState_ = false;
    lastRenderMs_ = 0;
    segmentDisplay.blank();
    logStateTransition(oldState, currentState_, "restore previous");
    renderCurrentState(nowMs, true);
    return;
  }

  installDefaultState(nowMs);
}

void DisplayManager::startDemoMessageState(uint32_t nowMs) {
  const DisplayState oldState = currentState_;
  DisplayState state;
  state.behavior = DisplayBehavior::kMessage;
  state.blink = true;
  state.payload.formatIndex = kDemoCountdownFormat;
  copyMessage(state.payload.message, settings_.finalMessage);

  demoCountdownActive_ = false;
  currentState_ = state;
  currentTransition_ = {true, nowMs + kDemoMessageMs};
  lastRenderMs_ = 0;
  blinkOn_ = true;
  blinkMs_ = nowMs;
  segmentDisplay.blank();
  logStateTransition(oldState, currentState_, "demo final message");
}

void DisplayManager::updateCountupOrigin(const ClockConfig& config) {
  countupOrigin_ = (strncmp(config.countupDatetime, "now", 3) == 0)
      ? rtcGetNow()
      : parseDateTime(config.countupDatetime);
}

uint32_t DisplayManager::refreshInterval() const {
  switch (currentState_.behavior) {
    case DisplayBehavior::kCountdown:
      if (currentState_.payload.formatIndex == kDemoCountdownFormat) {
        return kTenthMs;
      }
      return countdownHasTenths(currentState_.payload.formatIndex) ? kTenthMs : kSecondMs;
    case DisplayBehavior::kCountup:
      return countupHasTenths(currentState_.payload.formatIndex) ? kTenthMs : kSecondMs;
    case DisplayBehavior::kClock:
      return clockHasTenths(currentState_.payload.formatIndex) ? kTenthMs : kSecondMs;
    case DisplayBehavior::kMessage:
      return currentState_.blink ? kBlinkMs : kSecondMs;
  }
  return kSecondMs;
}

bool DisplayManager::renderElapsed(uint32_t nowMs, bool force) {
  if (force) {
    lastRenderMs_ = nowMs;
    return true;
  }

  if (static_cast<long>(nowMs - lastRenderMs_) < static_cast<long>(refreshInterval())) {
    return false;
  }
  lastRenderMs_ = nowMs;
  return true;
}

bool DisplayManager::transitionExpired(uint32_t nowMs) const {
  return currentTransition_.hasExpiration &&
         static_cast<long>(nowMs - currentTransition_.expiresAtMs) >= 0;
}

void DisplayManager::renderCurrentState(uint32_t nowMs, bool force) {
  switch (currentState_.behavior) {
    case DisplayBehavior::kClock:
      renderClock(nowMs, force);
      break;
    case DisplayBehavior::kCountdown:
      renderCountdown(nowMs, force);
      break;
    case DisplayBehavior::kCountup:
      renderCountup(nowMs, force);
      break;
    case DisplayBehavior::kMessage:
      renderMessage(nowMs, force);
      break;
  }
}

void DisplayManager::renderClock(uint32_t nowMs, bool force) {
  if (static_cast<long>(nowMs - colonMs_) >= static_cast<long>(kBlinkMs)) {
    colonMs_ = nowMs;
    colonVisible_ = !colonVisible_;
  }
  if (!renderElapsed(nowMs, force)) return;

  TimeFields fields = rtcToFields(rtcGetNow());
  if (clockHasTenths(currentState_.payload.formatIndex)) {
    fields.tenths = (nowMs % kSecondMs) / kTenthMs;
  }

  char r1[8], r2[8], r3[8];
  ::renderClock(currentState_.payload.formatIndex, fields, r1, r2, r3, colonVisible_);
  segmentDisplay.showPanels(r1, r2, r3);
}

void DisplayManager::renderCountdown(uint32_t nowMs, bool force) {
  if (!renderElapsed(nowMs, force)) return;

  const DateTime now = rtcGetNow();
  const long secs = static_cast<long>(currentState_.payload.endTime.unixtime()) -
                    static_cast<long>(now.unixtime());

  if (!hasPreviousState_ && !demoCountdownActive_ && currentState_.behavior == DisplayBehavior::kCountdown && secs <= 0) {
    DisplayState finalState;
    finalState.behavior = DisplayBehavior::kMessage;
    finalState.blink = false;
    copyMessage(finalState.payload.message, settings_.finalMessage);
    installState(finalState, {false, 0}, nowMs, false);
    renderCurrentState(nowMs, true);
    return;
  }

  if (currentState_.payload.formatIndex == kDemoCountdownFormat) {
    const uint32_t remaining = currentTransition_.expiresAtMs - nowMs;
    const uint8_t whole =
        static_cast<uint8_t>(min<uint32_t>(9, remaining / kSecondMs));
    const uint8_t tenths = static_cast<uint8_t>(
        min<uint32_t>(9, (remaining % kSecondMs) / kTenthMs));
    char r3[8];
    snprintf(r3, sizeof(r3), "%2u.%u", whole, tenths);
    segmentDisplay.showPanels("    ", "    ", r3);
    return;
  }

  TimeFields fields = deltaToFields(secs);
  if (countdownHasTenths(currentState_.payload.formatIndex)) {
    fields.tenths = (secs > 0) ? (10 - (nowMs % kSecondMs) / kTenthMs) % 10 : 0;
  }

  char r1[8], r2[8], r3[8];
  ::renderCountdown(currentState_.payload.formatIndex, fields, r1, r2, r3);
  segmentDisplay.showPanels(r1, r2, r3);
}

void DisplayManager::renderCountup(uint32_t nowMs, bool force) {
  if (!renderElapsed(nowMs, force)) return;

  const DateTime now = rtcGetNow();
  const long secs = static_cast<long>(now.unixtime()) -
                    static_cast<long>(currentState_.payload.startTime.unixtime());

  TimeFields fields = deltaToFields(secs);
  if (countupHasTenths(currentState_.payload.formatIndex)) {
    fields.tenths = (nowMs % kSecondMs) / kTenthMs;
  }

  char r1[8], r2[8], r3[8];
  ::renderCountup(currentState_.payload.formatIndex, fields, r1, r2, r3);
  segmentDisplay.showPanels(r1, r2, r3);
}

void DisplayManager::renderMessage(uint32_t nowMs, bool force) {
  if (currentState_.blink &&
      static_cast<long>(nowMs - blinkMs_) >= static_cast<long>(kBlinkMs)) {
    blinkMs_ = nowMs;
    blinkOn_ = !blinkOn_;
    force = true;
  }

  if (!renderElapsed(nowMs, force)) return;

  char r1[8], r2[8], r3[8];
  if (currentState_.blink && !blinkOn_) {
    blankBuffers(r1, r2, r3);
  } else {
    messageToBuffers(currentState_.payload.message, r1, r2, r3);
  }
  segmentDisplay.showPanels(r1, r2, r3);
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
