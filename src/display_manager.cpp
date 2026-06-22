#include "display_manager.h"

#include "clock_format.h"
#include "clock_state.h"
#include "display.h"
#include "log.h"

namespace {

constexpr uint32_t kSecondMs = 1000;
constexpr uint32_t kTenthMs = 100;
constexpr uint32_t kMessageBlinkMs = 500;
constexpr uint32_t kColonBlinkMs = 1000;
constexpr uint32_t kSplashDurationMs = 5000;
constexpr uint32_t kDemoCountdownMs = 5000;
constexpr uint32_t kDemoMessageMs = 5000;

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
  fields.dayOfWeek = dt.dayOfTheWeek();
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

void copyDisplayRow(char destination[kDisplayRowChars + 1], const char* source) {
  snprintf(destination, kDisplayRowChars + 1, "%-4.4s", source);
}

void copyDisplayTitle(char destination[kDisplayRowChars + 1], const char* source) {
  snprintf(destination, kDisplayRowChars + 1, "%4.4s", source);
}

}  // namespace

void DisplayManager::begin(const ClockConfig& config) {
  if (clockSource_ == nullptr) {
    clockSource_ = &systemClockSource();
  }
  applySettings(config);
}

void DisplayManager::setClockSource(ClockSource& clockSource) {
  clockSource_ = &clockSource;
}

void DisplayManager::applySettings(const ClockConfig& config) {
  if (clockSource_ == nullptr) {
    clockSource_ = &systemClockSource();
  }
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

void DisplayManager::setBrightness(uint8_t brightness) {
  settings_.brightness = constrain(brightness, 0, 7);
  segmentDisplay.setBrightness(settings_.brightness);
}

void DisplayManager::tick(uint32_t nowMs) {
  if (transitionExpired(nowMs)) {
    finishTemporaryState(nowMs);
  }

  renderCurrentState(nowMs);
}

void DisplayManager::showSplash(const char* message) {
  const uint32_t nowMs = millis();
  DisplayState state;
  state.behavior = DisplayBehavior::kMessage;
  copyMessage(state.payload.message, message);

  installState(state, {true, nowMs + kSplashDurationMs}, nowMs, true);
  renderCurrentState(nowMs, true);
}

void DisplayManager::showDemo() {
  const uint32_t nowMs = millis();
  DisplayState state;
  state.behavior = DisplayBehavior::kDemoCountdown;

  demoCountdownActive_ = true;
  installState(state, {true, nowMs + kDemoCountdownMs}, nowMs, true);
  renderCurrentState(nowMs, true);
}

void DisplayManager::showInfo(const char* message, int32_t durationMs) {
  const uint32_t nowMs = millis();
  DisplayState state;
  state.behavior = DisplayBehavior::kMessage;
  state.blink = true;
  copyMessage(state.payload.message, message);

  const bool expires = durationMs != FOREVER;
  const uint32_t expiresAt = expires ? nowMs + static_cast<uint32_t>(durationMs) : 0;
  installState(state, {expires, expiresAt}, nowMs, true);
  renderCurrentState(nowMs, true);
}

void DisplayManager::showPages(const DisplayPage* pages,
                               uint8_t pageCount,
                               uint16_t pageDurationMs,
                               bool repeat) {
  if (pages == nullptr || pageCount == 0) {
    return;
  }

  DisplayState state;
  const uint32_t nowMs = millis();
  state.behavior = DisplayBehavior::kPagedMessage;
  state.payload.paged.pageCount =
      pageCount < kMaxDisplayPages ? pageCount : kMaxDisplayPages;
  state.payload.paged.currentPage = 0;
  state.payload.paged.pageDurationMs = pageDurationMs;
  state.payload.paged.pageStartedAtMs = nowMs;
  state.payload.paged.repeat = repeat;

  for (uint8_t pageIndex = 0; pageIndex < state.payload.paged.pageCount; ++pageIndex) {
    for (uint8_t rowIndex = 0; rowIndex < kDisplayRowsPerPage; ++rowIndex) {
      if (rowIndex == 0) {
        copyDisplayTitle(state.payload.paged.pages[pageIndex].rows[rowIndex],
                         pages[pageIndex].rows[rowIndex]);
      } else {
        copyDisplayRow(state.payload.paged.pages[pageIndex].rows[rowIndex],
                       pages[pageIndex].rows[rowIndex]);
      }
    }
  }

  const uint32_t durationMs =
      static_cast<uint32_t>(state.payload.paged.pageDurationMs) *
      state.payload.paged.pageCount;
  installState(state, {!repeat, nowMs + durationMs}, nowMs, true);
  renderCurrentState(nowMs, true);
}

void DisplayManager::clearInfo() {
  if (hasPreviousState_ &&
      (currentState_.behavior == DisplayBehavior::kMessage ||
       currentState_.behavior == DisplayBehavior::kPagedMessage)) {
    finishTemporaryState(millis());
  }
}

const char* DisplayManager::currentStateName() const {
  return behaviorName(currentState_.behavior);
}

DisplayState DisplayManager::stateForConfiguredMode(PersistentMode mode) const {
  DisplayState state;

  switch (mode) {
    case kPersistentCountup:
      state.behavior = DisplayBehavior::kCountup;
      state.payload.countup.startTime = countupOrigin_;
      state.payload.countup.formatIndex = settings_.countupFmt;
      break;
    case kPersistentClock:
      state.behavior = DisplayBehavior::kClock;
      state.payload.clock.formatIndex = settings_.clockFmt;
      break;
    case kPersistentCountdown:
      state.behavior = DisplayBehavior::kCountdown;
      state.payload.countdown.endTime = parseDateTime(settings_.countdownDatetime);
      state.payload.countdown.formatIndex = settings_.countdownFmt;
      break;
  }

  return state;
}

const char* DisplayManager::behaviorName(DisplayBehavior behavior) const {
  switch (behavior) {
    case DisplayBehavior::kClock:          return "clock";
    case DisplayBehavior::kCountdown:      return "countdown";
    case DisplayBehavior::kCountup:        return "countup";
    case DisplayBehavior::kDemoCountdown:  return "demo-countdown";
    case DisplayBehavior::kMessage:        return "message";
    case DisplayBehavior::kPagedMessage:   return "pages";
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

  if (rememberPrevious && !hasPreviousState_) {
    previousState_ = currentState_;
    hasPreviousState_ = true;
  }

  currentState_ = state;
  currentTransition_ = transition;
  lastRenderMs_ = 0;
  blinkOn_ = true;
  blinkMs_ = nowMs;

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
    logStateTransition(oldState, currentState_, "restore previous");
    renderCurrentState(nowMs, true);
    return;
  }

  installDefaultState(nowMs);
}

void DisplayManager::startDemoMessageState(uint32_t nowMs) {
  DisplayState state;
  state.behavior = DisplayBehavior::kMessage;
  state.blink = true;
  copyMessage(state.payload.message, settings_.finalMessage);

  demoCountdownActive_ = false;
  installState(state, {true, nowMs + kDemoMessageMs}, nowMs, false);
  renderCurrentState(nowMs, true);
}

void DisplayManager::updateCountupOrigin(const ClockConfig& config) {
  countupOrigin_ = (strncmp(config.countupDatetime, "now", 3) == 0)
      ? clockSource_->now()
      : parseDateTime(config.countupDatetime);
}

uint32_t DisplayManager::refreshInterval() const {
  switch (currentState_.behavior) {
    case DisplayBehavior::kDemoCountdown:
      return kTenthMs;
    case DisplayBehavior::kCountdown:
      return countdownHasTenths(currentState_.payload.countdown.formatIndex) ? kTenthMs : kSecondMs;
    case DisplayBehavior::kCountup:
      return countupHasTenths(currentState_.payload.countup.formatIndex) ? kTenthMs : kSecondMs;
    case DisplayBehavior::kClock:
      return clockHasTenths(currentState_.payload.clock.formatIndex) ? kTenthMs : kSecondMs;
    case DisplayBehavior::kMessage:
      return currentState_.blink ? kMessageBlinkMs : kSecondMs;
    case DisplayBehavior::kPagedMessage:
      return currentState_.payload.paged.pageDurationMs;
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
    case DisplayBehavior::kClock:          renderClock(nowMs, force);          break;
    case DisplayBehavior::kCountdown:      renderCountdown(nowMs, force);      break;
    case DisplayBehavior::kCountup:        renderCountup(nowMs, force);        break;
    case DisplayBehavior::kDemoCountdown:  renderDemoCountdown(nowMs, force);  break;
    case DisplayBehavior::kMessage:        renderMessage(nowMs, force);        break;
    case DisplayBehavior::kPagedMessage:   renderPagedMessage(nowMs, force);   break;
  }
}

void DisplayManager::renderClock(uint32_t nowMs, bool force) {
  const uint8_t formatIndex = currentState_.payload.clock.formatIndex;
  if (clockBlinkColon(formatIndex) &&
      static_cast<long>(nowMs - colonMs_) >= static_cast<long>(kColonBlinkMs)) {
    colonMs_ = nowMs;
    colonVisible_ = !colonVisible_;
    force = true;
  }
  if (!renderElapsed(nowMs, force)) return;

  TimeFields fields = rtcToFields(clockSource_->now());
  if (clockHasTenths(formatIndex)) {
    fields.tenths = (nowMs % kSecondMs) / kTenthMs;
  }

  char r1[8], r2[8], r3[8];
  ::renderClock(formatIndex, fields, r1, r2, r3,
                !clockBlinkColon(formatIndex) || colonVisible_);
  segmentDisplay.showPanels(r1, r2, r3);
}

void DisplayManager::renderCountdown(uint32_t nowMs, bool force) {
  if (!renderElapsed(nowMs, force)) return;

  const uint8_t formatIndex = currentState_.payload.countdown.formatIndex;
  const DateTime now = clockSource_->now();
  const long secs = static_cast<long>(currentState_.payload.countdown.endTime.unixtime()) -
                    static_cast<long>(now.unixtime());

  if (!hasPreviousState_ && secs <= 0) {
    DisplayState finalState;
    finalState.behavior = DisplayBehavior::kMessage;
    finalState.blink = false;
    copyMessage(finalState.payload.message, settings_.finalMessage);
    installState(finalState, {false, 0}, nowMs, false);
    renderCurrentState(nowMs, true);
    return;
  }

  TimeFields fields = deltaToFields(secs);
  if (countdownHasTenths(formatIndex)) {
    fields.tenths = (secs > 0) ? (10 - (nowMs % kSecondMs) / kTenthMs) % 10 : 0;
  }

  char r1[8], r2[8], r3[8];
  ::renderCountdown(formatIndex, fields, r1, r2, r3);
  segmentDisplay.showPanels(r1, r2, r3);
}

void DisplayManager::renderCountup(uint32_t nowMs, bool force) {
  if (!renderElapsed(nowMs, force)) return;

  const uint8_t formatIndex = currentState_.payload.countup.formatIndex;
  const DateTime now = clockSource_->now();
  const long secs = static_cast<long>(now.unixtime()) -
                    static_cast<long>(currentState_.payload.countup.startTime.unixtime());

  TimeFields fields = deltaToFields(secs);
  if (countupHasTenths(formatIndex)) {
    fields.tenths = (nowMs % kSecondMs) / kTenthMs;
  }

  char r1[8], r2[8], r3[8];
  ::renderCountup(formatIndex, fields, r1, r2, r3);
  segmentDisplay.showPanels(r1, r2, r3);
}

void DisplayManager::renderDemoCountdown(uint32_t nowMs, bool force) {
  if (!renderElapsed(nowMs, force)) return;

  const uint32_t remaining = currentTransition_.expiresAtMs - nowMs;
  const uint8_t whole  = static_cast<uint8_t>(min<uint32_t>(9, remaining / kSecondMs));
  const uint8_t tenths = static_cast<uint8_t>(min<uint32_t>(9, (remaining % kSecondMs) / kTenthMs));
  char r3[8];
  snprintf(r3, sizeof(r3), "%2u.%u", whole, tenths);
  segmentDisplay.showPanels("    ", "    ", r3);
}

void DisplayManager::renderMessage(uint32_t nowMs, bool force) {
  if (currentState_.blink &&
      static_cast<long>(nowMs - blinkMs_) >= static_cast<long>(kMessageBlinkMs)) {
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

void DisplayManager::renderPagedMessage(uint32_t nowMs, bool force) {
  PagedDisplayPayload& paged = currentState_.payload.paged;
  if (paged.pageCount == 0) {
    return;
  }

  bool pageChanged = false;
  if (static_cast<long>(nowMs - paged.pageStartedAtMs) >=
      static_cast<long>(paged.pageDurationMs)) {
    const uint8_t nextPage = paged.currentPage + 1;
    if (nextPage < paged.pageCount) {
      paged.currentPage = nextPage;
      pageChanged = true;
    } else if (paged.repeat) {
      paged.currentPage = 0;
      pageChanged = true;
    }
    paged.pageStartedAtMs = nowMs;
  }

  if (pageChanged) {
    blinkOn_ = true;
    blinkMs_ = nowMs;
    force = true;
  } else if (static_cast<long>(nowMs - blinkMs_) >= static_cast<long>(kMessageBlinkMs)) {
    blinkMs_ = nowMs;
    blinkOn_ = !blinkOn_;
    force = true;
  }

  if (!force) {
    return;
  }

  const DisplayPage& page = paged.pages[paged.currentPage];
  segmentDisplay.showPanels(blinkOn_ ? page.rows[0] : "    ",
                            page.rows[1],
                            page.rows[2]);
}

DisplayManager displayManager;

void clockApplySettings(const ClockConfig& cfg) {
  displayManager.applySettings(cfg);
}

void clockSetBrightness(uint8_t brightness) {
  displayManager.setBrightness(brightness);
}

void clockTriggerDemo() {
  displayManager.showDemo();
}

void clockShowMessagePreview(const char* msg) {
  displayManager.showSplash(msg);
}

void clockShowInfo(const char* msg, int32_t durationMs) {
  displayManager.showInfo(msg, durationMs);
}

void clockClearInfo() {
  displayManager.clearInfo();
}
