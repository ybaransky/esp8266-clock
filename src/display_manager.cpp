#include "display_manager.h"

#include "clock_format.h"
#include "clock_state.h"
#include "display.h"
#include "friday_mode.h"
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
  baseView_ = viewForMode(config.activeMode);
  segmentDisplay.setBrightness(config.brightness);
  installView(millis());
}

void DisplayManager::setBrightness(uint8_t brightness) {
  settings_.brightness = constrain(brightness, 0, 7);
  segmentDisplay.setBrightness(settings_.brightness);
}

void DisplayManager::tick(uint32_t nowMs) {
  if (hasOverlay_ && overlayExpired(nowMs)) {
    finishOverlay(nowMs);
  }

  render(nowMs);
}

void DisplayManager::showSplash(const char* message) {
  const uint32_t nowMs = millis();
  OverlayState state;
  state.overlay = Overlay::kMessage;
  copyMessage(state.payload.message, message);
  state.transition = {true, nowMs + kSplashDurationMs};

  installOverlay(state, nowMs);
  render(nowMs, true);
}

void DisplayManager::showDemo() {
  const uint32_t nowMs = millis();
  OverlayState state;
  state.overlay = Overlay::kDemo;
  state.transition = {true, nowMs + kDemoCountdownMs};

  demoActive_ = true;
  installOverlay(state, nowMs);
  render(nowMs, true);
}

void DisplayManager::showInfo(const char* message, int32_t durationMs) {
  const uint32_t nowMs = millis();
  OverlayState state;
  state.overlay = Overlay::kMessage;
  state.blink = true;
  copyMessage(state.payload.message, message);

  const bool expires = durationMs != FOREVER;
  const uint32_t expiresAt = expires ? nowMs + static_cast<uint32_t>(durationMs) : 0;
  state.transition = {expires, expiresAt};

  installOverlay(state, nowMs);
  render(nowMs, true);
}

void DisplayManager::showPages(const DisplayPage* pages,
                               uint8_t pageCount,
                               uint16_t pageDurationMs,
                               bool repeat) {
  if (pages == nullptr || pageCount == 0) {
    return;
  }

  const uint32_t nowMs = millis();
  OverlayState state;
  state.overlay = Overlay::kPagedMessage;
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
  state.transition = {!repeat, nowMs + durationMs};

  installOverlay(state, nowMs);
  render(nowMs, true);
}

void DisplayManager::clearOverlay() {
  if (hasOverlay_ &&
      (overlay_.overlay == Overlay::kMessage || overlay_.overlay == Overlay::kPagedMessage)) {
    finishOverlay(millis());
  }
}

const char* DisplayManager::renderedName() const {
  return hasOverlay_ ? overlayName(overlay_.overlay) : viewName(baseView_.view);
}

ViewState DisplayManager::viewForMode(Mode mode) const {
  ViewState state;

  switch (mode) {
    case kModeCountup:
      state.view = View::kCountup;
      state.payload.countup.startTime = countupOrigin_;
      state.payload.countup.formatIndex = settings_.countupFmt;
      break;
    case kModeClock:
      state.view = View::kClock;
      state.payload.clock.formatIndex = settings_.clockFmt;
      break;
    case kModeCountdown:
      state.view = View::kCountdown;
      state.payload.countdown.endTime = parseDateTime(settings_.countdownDatetime);
      state.payload.countdown.formatIndex = settings_.countdownFmt;
      break;
    case kModeFriday:
      // FridayModeController will call setView() on the next tick.
      // Use the friday clock format as a safe initial view.
      state.view = View::kClock;
      state.payload.clock.formatIndex = settings_.fridayClockFmt;
      break;
  }

  return state;
}

void DisplayManager::setView(const ViewState& state) {
  const char* oldName = renderedName();
  baseView_ = state;
  if (hasOverlay_) {
    // Becomes visible once the active overlay clears - there's no separate
    // snapshot to keep in sync, since renderedName()/render() always read
    // baseView_ live at that point.
    return;
  }

  demoActive_ = false;
  const uint32_t nowMs = millis();
  lastRenderMs_ = 0;
  logTransition(oldName, viewName(baseView_.view), "view update");
  render(nowMs, true);
}

const char* viewName(View view) {
  switch (view) {
    case View::kClock:     return "clock";
    case View::kCountdown: return "countdown";
    case View::kCountup:   return "countup";
  }
  return "?";
}

const char* overlayName(Overlay overlay) {
  switch (overlay) {
    case Overlay::kDemo:         return "demo";
    case Overlay::kMessage:      return "message";
    case Overlay::kPagedMessage: return "pages";
  }
  return "?";
}

void DisplayManager::logTransition(const char* from, const char* to, const char* reason) const {
  LOG_PRINTF("display: %s -> %s (%s)\n", from, to, reason);
}

void DisplayManager::installOverlay(const OverlayState& state, uint32_t nowMs) {
  const char* oldName = renderedName();
  overlay_ = state;
  hasOverlay_ = true;
  lastRenderMs_ = 0;
  blinkOn_ = true;
  blinkMs_ = nowMs;
  logTransition(oldName, overlayName(overlay_.overlay), "overlay");
}

void DisplayManager::installView(uint32_t nowMs, bool forceRender) {
  const char* oldName = renderedName();
  demoActive_ = false;
  hasOverlay_ = false;
  lastRenderMs_ = 0;
  blinkOn_ = true;
  blinkMs_ = nowMs;
  logTransition(oldName, viewName(baseView_.view), "view install");
  if (forceRender) {
    render(nowMs, true);
  }
}

void DisplayManager::finishOverlay(uint32_t nowMs) {
  if (demoActive_) {
    startDemoMessageOverlay(nowMs);
    return;
  }

  clearOverlayAndRenderView(nowMs);
}

void DisplayManager::clearOverlayAndRenderView(uint32_t nowMs) {
  const char* oldName = renderedName();
  hasOverlay_ = false;
  lastRenderMs_ = 0;
  logTransition(oldName, viewName(baseView_.view), "overlay cleared");
  render(nowMs, true);
}

void DisplayManager::startDemoMessageOverlay(uint32_t nowMs) {
  OverlayState state;
  state.overlay = Overlay::kMessage;
  state.blink = true;
  copyMessage(state.payload.message, settings_.finalMessage);
  state.transition = {true, nowMs + kDemoMessageMs};

  demoActive_ = false;
  installOverlay(state, nowMs);
  render(nowMs, true);
}

void DisplayManager::updateCountupOrigin(const ClockConfig& config) {
  countupOrigin_ = (strncmp(config.countupDatetime, "now", 3) == 0)
      ? clockSource_->now()
      : parseDateTime(config.countupDatetime);
}

uint32_t DisplayManager::refreshInterval() const {
  if (hasOverlay_) {
    switch (overlay_.overlay) {
      case Overlay::kDemo:
        return kTenthMs;
      case Overlay::kMessage:
        return overlay_.blink ? kMessageBlinkMs : kSecondMs;
      case Overlay::kPagedMessage:
        return overlay_.payload.paged.pageDurationMs;
    }
    return kSecondMs;
  }

  switch (baseView_.view) {
    case View::kCountdown:
      return countdownHasTenths(baseView_.payload.countdown.formatIndex) ? kTenthMs : kSecondMs;
    case View::kCountup:
      return countupHasTenths(baseView_.payload.countup.formatIndex) ? kTenthMs : kSecondMs;
    case View::kClock:
      return clockHasTenths(baseView_.payload.clock.formatIndex) ? kTenthMs : kSecondMs;
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

bool DisplayManager::overlayExpired(uint32_t nowMs) const {
  return overlay_.transition.hasExpiration &&
         static_cast<long>(nowMs - overlay_.transition.expiresAtMs) >= 0;
}

void DisplayManager::render(uint32_t nowMs, bool force) {
  if (hasOverlay_) {
    switch (overlay_.overlay) {
      case Overlay::kDemo:         renderDemo(nowMs, force);         break;
      case Overlay::kMessage:      renderMessage(nowMs, force);      break;
      case Overlay::kPagedMessage: renderPagedMessage(nowMs, force); break;
    }
    return;
  }

  switch (baseView_.view) {
    case View::kClock:     renderClock(nowMs, force);     break;
    case View::kCountdown: renderCountdown(nowMs, force); break;
    case View::kCountup:   renderCountup(nowMs, force);   break;
  }
}

void DisplayManager::renderClock(uint32_t nowMs, bool force) {
  const uint8_t formatIndex = baseView_.payload.clock.formatIndex;
  if (clockBlinkColon(formatIndex) &&
      static_cast<long>(nowMs - colonMs_) >= static_cast<long>(kColonBlinkMs)) {
    colonMs_ = nowMs;
    colonVisible_ = !colonVisible_;
    force = true;
  }
  if (!renderElapsed(nowMs, force)) return;

  TimeFields fields = rtcToFields(clockSource_->now());
  if (settings_.clockUse12Hour) {
    fields.hours = fields.hours % 12;
    if (fields.hours == 0) fields.hours = 12;
  }
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

  const uint8_t formatIndex = baseView_.payload.countdown.formatIndex;
  const DateTime now = clockSource_->now();
  const long secs = static_cast<long>(baseView_.payload.countdown.endTime.unixtime()) -
                    static_cast<long>(now.unixtime());

  if (secs <= 0) {
    OverlayState state;
    state.overlay = Overlay::kMessage;
    copyMessage(state.payload.message, settings_.finalMessage);
    // No expiration set: the countdown has finished, so this stays up until
    // the next mode/config change installs a new view.
    installOverlay(state, nowMs);
    render(nowMs, true);
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

  const uint8_t formatIndex = baseView_.payload.countup.formatIndex;
  const DateTime now = clockSource_->now();
  const long secs = static_cast<long>(now.unixtime()) -
                    static_cast<long>(baseView_.payload.countup.startTime.unixtime());

  TimeFields fields = deltaToFields(secs);
  if (countupHasTenths(formatIndex)) {
    fields.tenths = (nowMs % kSecondMs) / kTenthMs;
  }

  char r1[8], r2[8], r3[8];
  ::renderCountup(formatIndex, fields, r1, r2, r3);
  segmentDisplay.showPanels(r1, r2, r3);
}

void DisplayManager::renderDemo(uint32_t nowMs, bool force) {
  if (!renderElapsed(nowMs, force)) return;

  const uint32_t remaining = overlay_.transition.expiresAtMs - nowMs;
  const uint8_t whole  = static_cast<uint8_t>(min<uint32_t>(9, remaining / kSecondMs));
  const uint8_t tenths = static_cast<uint8_t>(min<uint32_t>(9, (remaining % kSecondMs) / kTenthMs));
  char r3[8];
  snprintf(r3, sizeof(r3), "%2u.%u", whole, tenths);
  segmentDisplay.showPanels("    ", "    ", r3);
}

void DisplayManager::renderMessage(uint32_t nowMs, bool force) {
  if (overlay_.blink &&
      static_cast<long>(nowMs - blinkMs_) >= static_cast<long>(kMessageBlinkMs)) {
    blinkMs_ = nowMs;
    blinkOn_ = !blinkOn_;
    force = true;
  }

  if (!renderElapsed(nowMs, force)) return;

  char r1[8], r2[8], r3[8];
  if (overlay_.blink && !blinkOn_) {
    blankBuffers(r1, r2, r3);
  } else {
    messageToBuffers(overlay_.payload.message, r1, r2, r3);
  }
  segmentDisplay.showPanels(r1, r2, r3);
}

void DisplayManager::renderPagedMessage(uint32_t nowMs, bool force) {
  PagedDisplayPayload& paged = overlay_.payload.paged;
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
  fridayModeApplySettings(cfg);
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
  displayManager.clearOverlay();
}
