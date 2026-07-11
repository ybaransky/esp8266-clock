#include "display_manager.h"

#include "clock_state.h"
#include "display.h"
#include "display_renderer.h"
#include "friday_mode.h"
#include "log.h"
#include "rtc_ds3231.h"

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

void copyMessage(char destination[64], const char* source) {
  strncpy(destination, source, 63);
  destination[63] = '\0';
}

void copyDisplayRow(char destination[kDisplayRowChars + 1], const char* source) {
  snprintf(destination, kDisplayRowChars + 1, "%-4.4s", source);
}

void copyDisplayTitle(char destination[kDisplayRowChars + 1], const char* source) {
  snprintf(destination, kDisplayRowChars + 1, "%4.4s", source);
}

}  // namespace

void DisplayManager::begin(const ClockConfig& config) {
  applySettings(config);
}

void DisplayManager::applySettings(const ClockConfig& config) {
  settings_ = config;
  scheduler_.reset(millis());

  updateCountupOrigin(config);
  baseView_ = viewForMode(config.activeMode);
  segmentDisplay.setBrightness(config.display.brightness);
  installView(millis());
}

void DisplayManager::setBrightness(uint8_t brightness) {
  settings_.display.brightness = constrain(brightness, 0, 7);
  segmentDisplay.setBrightness(settings_.display.brightness);
}

void DisplayManager::tick(uint32_t nowMs) {
  if (hasOverlay() && overlayExpired(nowMs)) {
    finishOverlay(nowMs);
  }

  render(nowMs);
}

void DisplayManager::notifySecondBoundary() {
  scheduler_.invalidateRender();
}

void DisplayManager::showSplash(const char* message) {
  const uint32_t nowMs = millis();
  OverlayState state;
  state.overlay = Overlay::kMessage;
  copyMessage(state.message, message);
  state.transition = {true, nowMs + kSplashDurationMs};

  installOverlay(state, nowMs);
  render(nowMs, true);
}

void DisplayManager::showDemo() {
  const uint32_t nowMs = millis();
  OverlayState state;
  state.overlay = Overlay::kDemo;
  state.chainFinalMessage = true;
  state.transition = {true, nowMs + kDemoCountdownMs};

  installOverlay(state, nowMs);
  render(nowMs, true);
}

void DisplayManager::showInfo(const char* message, int32_t durationMs) {
  const uint32_t nowMs = millis();
  OverlayState state;
  state.overlay = Overlay::kMessage;
  state.blink = true;
  copyMessage(state.message, message);

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
  state.paged.pageCount =
      pageCount < kMaxDisplayPages ? pageCount : kMaxDisplayPages;
  state.paged.currentPage = 0;
  state.paged.pageDurationMs = pageDurationMs;
  state.paged.pageStartedAtMs = nowMs;
  state.paged.repeat = repeat;

  for (uint8_t pageIndex = 0; pageIndex < state.paged.pageCount; ++pageIndex) {
    for (uint8_t rowIndex = 0; rowIndex < kDisplayRowsPerPage; ++rowIndex) {
      if (rowIndex == 0) {
        copyDisplayTitle(state.paged.pages[pageIndex].rows[rowIndex],
                         pages[pageIndex].rows[rowIndex]);
      } else {
        copyDisplayRow(state.paged.pages[pageIndex].rows[rowIndex],
                       pages[pageIndex].rows[rowIndex]);
      }
    }
  }

  const uint32_t durationMs =
      static_cast<uint32_t>(state.paged.pageDurationMs) * state.paged.pageCount;
  state.transition = {!repeat, nowMs + durationMs};

  installOverlay(state, nowMs);
  render(nowMs, true);
}

void DisplayManager::clearOverlay() {
  if (hasOverlay() &&
      (overlay_.overlay == Overlay::kMessage || overlay_.overlay == Overlay::kPagedMessage)) {
    finishOverlay(millis());
  }
}

const char* DisplayManager::renderedName() const {
  return hasOverlay() ? overlayName(overlay_.overlay) : viewName(baseView_.view);
}

ViewState DisplayManager::viewForMode(Mode mode) const {
  ViewState state;

  switch (mode) {
    case kModeCountup:
      state.view = View::kCountup;
      state.anchor = countupOrigin_;
      state.formatIndex = settings_.display.countupFmt;
      break;
    case kModeClock:
      state.view = View::kClock;
      state.formatIndex = settings_.display.clockFmt;
      break;
    case kModeCountdown:
      state.view = View::kCountdown;
      state.anchor = parseDateTime(settings_.countdownDatetime);
      state.formatIndex = settings_.display.countdownFmt;
      break;
    case kModeFriday:
      // FridayModeController will call setView() on the next tick.
      // Use the friday clock format as a safe initial view.
      state.view = View::kClock;
      state.formatIndex = settings_.fridayClockFmt;
      break;
  }

  return state;
}

void DisplayManager::setView(const ViewState& state) {
  const char* oldName = renderedName();
  baseView_ = state;
  if (hasOverlay()) {
    // Becomes visible once the active overlay clears - there's no separate
    // snapshot to keep in sync, since renderedName()/render() always read
    // baseView_ live at that point.
    return;
  }

  const uint32_t nowMs = millis();
  scheduler_.invalidateRender();
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
    case Overlay::kNone:         return "none";
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
  scheduler_.invalidateRender();
  scheduler_.resetBlink(nowMs);
  logTransition(oldName, overlayName(overlay_.overlay), "overlay");
}

void DisplayManager::installView(uint32_t nowMs, bool forceRender) {
  const char* oldName = renderedName();
  overlay_.overlay = Overlay::kNone;
  scheduler_.invalidateRender();
  scheduler_.resetBlink(nowMs);
  logTransition(oldName, viewName(baseView_.view), "view install");
  if (forceRender) {
    render(nowMs, true);
  }
}

void DisplayManager::finishOverlay(uint32_t nowMs) {
  if (overlay_.chainFinalMessage) {
    startDemoMessageOverlay(nowMs);
    return;
  }

  clearOverlayAndRenderView(nowMs);
}

void DisplayManager::clearOverlayAndRenderView(uint32_t nowMs) {
  const char* oldName = renderedName();
  overlay_.overlay = Overlay::kNone;
  scheduler_.invalidateRender();
  logTransition(oldName, viewName(baseView_.view), "overlay cleared");
  render(nowMs, true);
}

void DisplayManager::startDemoMessageOverlay(uint32_t nowMs) {
  OverlayState state;
  state.overlay = Overlay::kMessage;
  state.blink = true;
  copyMessage(state.message, settings_.finalMessage);
  state.transition = {true, nowMs + kDemoMessageMs};

  installOverlay(state, nowMs);
  render(nowMs, true);
}

void DisplayManager::updateCountupOrigin(const ClockConfig& config) {
  countupOrigin_ = (strncmp(config.countupDatetime, "now", 3) == 0)
      ? rtcGetNowCached()
      : parseDateTime(config.countupDatetime);
}

uint32_t DisplayManager::refreshInterval() const {
  if (hasOverlay()) {
    switch (overlay_.overlay) {
      case Overlay::kNone:
        break;
      case Overlay::kDemo:
        return kTenthMs;
      case Overlay::kMessage:
        return overlay_.blink ? kMessageBlinkMs : kSecondMs;
      case Overlay::kPagedMessage:
        return overlay_.paged.pageDurationMs;
    }
    return kSecondMs;
  }

  switch (baseView_.view) {
    case View::kCountdown:
      return formatHasTenths(kFmtGroupCountdown, baseView_.formatIndex) ? kTenthMs : kSecondMs;
    case View::kCountup:
      return formatHasTenths(kFmtGroupCountUp, baseView_.formatIndex) ? kTenthMs : kSecondMs;
    case View::kClock:
      return formatHasTenths(kFmtGroupClock, baseView_.formatIndex) ? kTenthMs : kSecondMs;
  }
  return kSecondMs;
}

bool DisplayManager::renderElapsed(uint32_t nowMs, bool force) {
  return scheduler_.shouldRender(nowMs, refreshInterval(), force);
}

bool DisplayManager::overlayExpired(uint32_t nowMs) const {
  return overlay_.transition.hasExpiration &&
         static_cast<long>(nowMs - overlay_.transition.expiresAtMs) >= 0;
}

void DisplayManager::render(uint32_t nowMs, bool force) {
  if (hasOverlay()) {
    switch (overlay_.overlay) {
      case Overlay::kNone:         break;
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
  const uint8_t formatIndex = baseView_.formatIndex;
  if (clockFormatBlinksColon(formatIndex) && scheduler_.toggleColonIfDue(nowMs, kColonBlinkMs)) {
    force = true;
  }
  if (!renderElapsed(nowMs, force)) return;

  // Cached read: this runs up to 10x/sec for tenths formats, and the RTC's
  // registers only change once a second anyway.
  const DateTime now = rtcGetNowCached();
  uint8_t tenths = 0;
  if (formatHasTenths(kFmtGroupClock, formatIndex)) {
    tenths = rtcMsIntoSecond(nowMs) / kTenthMs;
  }

  const DisplayFrame frame = renderClockDisplayFrame(
      formatIndex, now, settings_.display.clockUse12Hour, tenths,
      !clockFormatBlinksColon(formatIndex) || scheduler_.colonVisible());
  segmentDisplay.showFrame(frame);
}

void DisplayManager::renderCountdown(uint32_t nowMs, bool force) {
  if (!renderElapsed(nowMs, force)) return;

  const uint8_t formatIndex = baseView_.formatIndex;
  const DateTime now = rtcGetNowCached();
  const long secs = static_cast<long>(baseView_.anchor.unixtime()) -
                    static_cast<long>(now.unixtime());

  if (secs <= 0) {
    OverlayState state;
    state.overlay = Overlay::kMessage;
    copyMessage(state.message, settings_.finalMessage);
    // No expiration set: the countdown has finished, so this stays up until
    // the next mode/config change installs a new view.
    installOverlay(state, nowMs);
    render(nowMs, true);
    return;
  }

  uint8_t tenths = 0;
  if (formatHasTenths(kFmtGroupCountdown, formatIndex)) {
    tenths = (secs > 0) ? (10 - rtcMsIntoSecond(nowMs) / kTenthMs) % 10 : 0;
  }

  const DisplayFrame frame =
      renderCountdownDisplayFrame(formatIndex, secs, tenths);
  segmentDisplay.showFrame(frame);
}

void DisplayManager::renderCountup(uint32_t nowMs, bool force) {
  if (!renderElapsed(nowMs, force)) return;

  const uint8_t formatIndex = baseView_.formatIndex;
  const DateTime now = rtcGetNowCached();
  const long secs = static_cast<long>(now.unixtime()) -
                    static_cast<long>(baseView_.anchor.unixtime());

  uint8_t tenths = 0;
  if (formatHasTenths(kFmtGroupCountUp, formatIndex)) {
    tenths = rtcMsIntoSecond(nowMs) / kTenthMs;
  }

  const DisplayFrame frame = renderCountupDisplayFrame(formatIndex, secs, tenths);
  segmentDisplay.showFrame(frame);
}

void DisplayManager::renderDemo(uint32_t nowMs, bool force) {
  if (!renderElapsed(nowMs, force)) return;

  const uint32_t remaining = overlay_.transition.expiresAtMs - nowMs;
  const uint8_t whole  = static_cast<uint8_t>(min<uint32_t>(9, remaining / kSecondMs));
  const uint8_t tenths = static_cast<uint8_t>(min<uint32_t>(9, (remaining % kSecondMs) / kTenthMs));
  const DisplayFrame frame = renderDemoDisplayFrame(whole, tenths);
  segmentDisplay.showFrame(frame);
}

void DisplayManager::renderMessage(uint32_t nowMs, bool force) {
  if (overlay_.blink && scheduler_.toggleBlinkIfDue(nowMs, kMessageBlinkMs)) {
    force = true;
  }

  if (!renderElapsed(nowMs, force)) return;

  const DisplayFrame frame = renderMessageDisplayFrame(
      overlay_.message, !overlay_.blink || scheduler_.blinkOn());
  segmentDisplay.showFrame(frame);
}

void DisplayManager::renderPagedMessage(uint32_t nowMs, bool force) {
  PagedDisplayPayload& paged = overlay_.paged;
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
    scheduler_.resetBlink(nowMs);
    force = true;
  } else if (scheduler_.toggleBlinkIfDue(nowMs, kMessageBlinkMs)) {
    force = true;
  }

  if (!force) {
    return;
  }

  const DisplayPage& page = paged.pages[paged.currentPage];
  const DisplayFrame frame = renderPageDisplayFrame(
      page.rows[0], page.rows[1], page.rows[2], scheduler_.blinkOn());
  segmentDisplay.showFrame(frame);
}

DisplayManager displayManager;

void clockApplySettings(const ClockConfig& cfg) {
  displayManager.applySettings(cfg);
  fridayModeApplySettings(cfg);
}
