#include "display_manager.h"

#include "display_format.h"
#include "display.h"
#include "display_renderer.h"
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
constexpr long kLongRangeSeconds = 24L * 3600L;

DateTime parseDateTime(const char* s) {
  int y = 2000, mo = 1, d = 1, h = 0, mi = 0, sec = 0;
  sscanf(s, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &h, &mi, &sec);
  return DateTime(y, mo, d, h, mi, sec);
}

void copyMessage(char destination[64], const char* source) {
  strncpy(destination, source, 63);
  destination[63] = '\0';
}

void copyDisplayPanel(char destination[kDisplayPanelChars + 1], const char* source) {
  snprintf(destination, kDisplayPanelChars + 1, "%-4.4s", source);
}

void copyDisplayTitle(char destination[kDisplayPanelChars + 1], const char* source) {
  snprintf(destination, kDisplayPanelChars + 1, "%4.4s", source);
}

}  // namespace

// -----------------------------------------------------------------------------
// DisplayScheduler
// -----------------------------------------------------------------------------

void DisplayScheduler::reset(uint32_t nowMs) {
  blinkOn_ = true;
  blinkMs_ = nowMs;
  colonVisible_ = true;
  colonMs_ = 0;
  lastRenderMs_ = 0;
}

void DisplayScheduler::resetBlink(uint32_t nowMs) {
  blinkOn_ = true;
  blinkMs_ = nowMs;
}

void DisplayScheduler::invalidateRender() { lastRenderMs_ = 0; }

bool DisplayScheduler::shouldRender(uint32_t nowMs, uint32_t intervalMs,
                                    bool force) {
  if (!force && (static_cast<long>(nowMs - lastRenderMs_) <
                 static_cast<long>(intervalMs))) return false;
  lastRenderMs_ = nowMs;
  return true;
}

bool DisplayScheduler::toggleBlinkIfDue(uint32_t nowMs, uint32_t intervalMs) {
  if (static_cast<long>(nowMs - blinkMs_) < static_cast<long>(intervalMs))
    return false;
  blinkMs_ = nowMs;
  blinkOn_ = !blinkOn_;
  return true;
}

bool DisplayScheduler::toggleColonIfDue(uint32_t nowMs, uint32_t intervalMs) {
  if (static_cast<long>(nowMs - colonMs_) < static_cast<long>(intervalMs))
    return false;
  colonMs_ = nowMs;
  colonVisible_ = !colonVisible_;
  return true;
}

// -----------------------------------------------------------------------------
// DisplaySettings
// -----------------------------------------------------------------------------

DisplaySettings DisplaySettings::fromConfig(const ClockConfig& config) {
  DisplaySettings settings;
  settings.activeMode = config.activeMode;
  settings.display = config.display;
  settings.countdown = config.countdown;
  settings.countupFormat = config.countup.format;
  settings.fridayClockFmt = config.friday.clockFmt;
  settings.trading = config.trading;
  strlcpy(settings.finalMessage, config.messages.final,
          sizeof(settings.finalMessage));
  return settings;
}

// -----------------------------------------------------------------------------
// DisplayManager
// -----------------------------------------------------------------------------

void DisplayManager::applySettings(const ClockConfig& config) {
  settings_ = DisplaySettings::fromConfig(config);
  scheduler_.reset(millis());

  updateCountupOrigin(config);
  baseView_ = viewForMode(config.activeMode);
  display_.setBrightness(config.display.brightness);
  installView(millis());
}

void DisplayManager::setBrightness(uint8_t brightness) {
  settings_.display.brightness = constrain(brightness, 0, 7);
  display_.setBrightness(settings_.display.brightness);
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
  state.overlay = Overlay::kSplash;
  copyMessage(state.message, message);
  state.transition = {true, nowMs + kSplashDurationMs};

  installOverlay(state, nowMs);
  render(nowMs, true);
}

void DisplayManager::showDemo() {
  const uint32_t nowMs = millis();
  OverlayState state;
  state.overlay = Overlay::kDemoCountdown;
  state.transition = {true, nowMs + kDemoCountdownMs};

  installOverlay(state, nowMs);
  render(nowMs, true);
}

void DisplayManager::showInfo(const char* message, int32_t durationMs) {
  const uint32_t nowMs = millis();
  OverlayState state;
  state.overlay = Overlay::kBlinkingMessage;
  copyMessage(state.message, message);

  const bool expires = durationMs != kForever;
  const uint32_t expiresAt = expires ? nowMs + static_cast<uint32_t>(durationMs) : 0;
  state.transition = {expires, expiresAt};

  installOverlay(state, nowMs);
  render(nowMs, true);
}

void DisplayManager::showPages(const DisplayPage* pages,
                               uint8_t pageCount,
                               uint16_t pageDurationMs,
                               bool repeat) {
  if ((pages == nullptr) || (pageCount == 0)) {
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
    for (uint8_t panelIndex = 0; panelIndex < kDisplayPanelsPerPage; ++panelIndex) {
      if (panelIndex == 0) {
        copyDisplayTitle(state.paged.pages[pageIndex].panels[panelIndex],
                         pages[pageIndex].panels[panelIndex]);
      } else {
        copyDisplayPanel(state.paged.pages[pageIndex].panels[panelIndex],
                         pages[pageIndex].panels[panelIndex]);
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
  if (hasOverlay() && (overlay_.overlay != Overlay::kDemoCountdown)) {
    finishOverlay(millis());
  }
}

bool DisplayManager::demoActive() const {
  return (overlay_.overlay == Overlay::kDemoCountdown) ||
         (overlay_.overlay == Overlay::kDemoFinalMessage);
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
      state.formatIndex = settings_.countupFormat;
      break;
    case kModeClock:
      state.view = View::kClock;
      state.formatIndex = settings_.display.clockFmt;
      break;
    case kModeCountdown:
      state.view = View::kCountdown;
      state.anchor = parseDateTime(settings_.countdown.end);
      state.formatIndex = settings_.countdown.format;
      break;
    case kModeFriday:
      // FridayModeController will call setView() on the next tick.
      // Use the friday clock format as a safe initial view.
      state.view = View::kClock;
      state.formatIndex = settings_.fridayClockFmt;
      break;
    case kModeTrading:
      // TradingModeController replaces this placeholder immediately after a
      // config apply and then at each live open/close boundary.
      state.view = View::kCountdown;
      state.anchor = rtc_.getNowCached() + TimeSpan(1);
      state.formatIndex = settings_.trading.format;
      state.longFormatIndex = settings_.trading.formatOver24;
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
    case Overlay::kNone:              return "none";
    case Overlay::kSplash:            return "splash";
    case Overlay::kBlinkingMessage:   return "message";
    case Overlay::kCountdownComplete: return "complete";
    case Overlay::kDemoCountdown:
    case Overlay::kDemoFinalMessage:  return "demo";
    case Overlay::kPagedMessage:      return "pages";
  }
  return "?";
}

void DisplayManager::logTransition(const char* from, const char* to, const char* reason) const {
  LOG_PRINTF("display: %s -> %s (%s)", from, to, reason);
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
  if (overlay_.overlay == Overlay::kDemoCountdown) {
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
  state.overlay = Overlay::kDemoFinalMessage;
  copyMessage(state.message, settings_.finalMessage);
  state.transition = {true, nowMs + kDemoMessageMs};

  installOverlay(state, nowMs);
  render(nowMs, true);
}

void DisplayManager::updateCountupOrigin(const ClockConfig& config) {
  countupOrigin_ = (strncmp(config.countup.start, "now", 3) == 0)
      ? rtc_.getNowCached()
      : parseDateTime(config.countup.start);
}

// Selects the counting format for the base view's current duration. Evaluated
// fresh on every render (and for the render cadence), so the display switches
// to the long-range format at >= 24h remaining/elapsed and reverts on its own
// the moment the duration drops below 24h - no crossing state is kept.
uint8_t DisplayManager::activeCountingFormatIndex() const {
  if (baseView_.longFormatIndex == kSameFormat) return baseView_.formatIndex;
  const long nowUnix = static_cast<long>(rtc_.getNowCached().unixtime());
  const long anchorUnix = static_cast<long>(baseView_.anchor.unixtime());
  const long secs = (baseView_.view == View::kCountup) ? (nowUnix - anchorUnix)
                                                       : (anchorUnix - nowUnix);
  return (secs >= kLongRangeSeconds) ? baseView_.longFormatIndex
                                     : baseView_.formatIndex;
}

uint32_t DisplayManager::refreshInterval() const {
  if (hasOverlay()) {
    switch (overlay_.overlay) {
      case Overlay::kNone:
        break;
      case Overlay::kDemoCountdown:
        return kTenthMs;
      case Overlay::kBlinkingMessage:
      case Overlay::kDemoFinalMessage:
        return kMessageBlinkMs;
      case Overlay::kSplash:
      case Overlay::kCountdownComplete:
        return kSecondMs;
      case Overlay::kPagedMessage:
        return overlay_.paged.pageDurationMs;
    }
    return kSecondMs;
  }

  switch (baseView_.view) {
    case View::kCountdown:
      return displayFormatInfo(kFmtGroupCountdown, activeCountingFormatIndex())
                     .refreshRate == RefreshRate::kOneTenth
                 ? kTenthMs
                 : kSecondMs;
    case View::kCountup:
      return displayFormatInfo(kFmtGroupCountUp, activeCountingFormatIndex())
                     .refreshRate == RefreshRate::kOneTenth
                 ? kTenthMs
                 : kSecondMs;
    case View::kClock:
      return displayFormatInfo(kFmtGroupClock, baseView_.formatIndex).refreshRate ==
                     RefreshRate::kOneTenth
                 ? kTenthMs
                 : kSecondMs;
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

bool DisplayManager::overlayBlinks() const {
  return (overlay_.overlay == Overlay::kBlinkingMessage) ||
         (overlay_.overlay == Overlay::kDemoFinalMessage);
}

void DisplayManager::render(uint32_t nowMs, bool force) {
  DisplayFrame frame;
  bool frameReady = false;
  if (hasOverlay()) {
    switch (overlay_.overlay) {
      case Overlay::kNone:              break;
      case Overlay::kDemoCountdown:
        frameReady = buildDemoFrame(nowMs, force, frame);
        break;
      case Overlay::kSplash:
      case Overlay::kBlinkingMessage:
      case Overlay::kCountdownComplete:
      case Overlay::kDemoFinalMessage:
        frameReady = buildMessageFrame(nowMs, force, frame);
        break;
      case Overlay::kPagedMessage:
        frameReady = buildPagedMessageFrame(nowMs, force, frame);
        break;
    }
  } else {
    switch (baseView_.view) {
      case View::kClock:
        frameReady = buildClockFrame(nowMs, force, frame);
        break;
      case View::kCountdown:
        frameReady = buildCountdownFrame(nowMs, force, frame);
        break;
      case View::kCountup:
        frameReady = buildCountupFrame(nowMs, force, frame);
        break;
    }
  }

  if (frameReady) display_.showFrame(frame);
}

bool DisplayManager::buildClockFrame(uint32_t nowMs, bool force,
                                     DisplayFrame& frame) {
  const uint8_t formatIndex = baseView_.formatIndex;
  const DisplayFormatInfo& format =
      displayFormatInfo(kFmtGroupClock, formatIndex);
  if ((format.colonAnimation == ColonAnimation::kBlinking) &&
      scheduler_.toggleColonIfDue(nowMs, kColonBlinkMs)) {
    force = true;
  }
  if (!renderElapsed(nowMs, force)) return false;

  // Cached read: this runs up to 10x/sec for tenths formats, and the RTC's
  // registers only change once a second anyway.
  const DateTime now = rtc_.getNowCached();
  uint8_t tenths = 0;
  if (format.refreshRate == RefreshRate::kOneTenth) {
    tenths = rtc_.msIntoSecond(nowMs) / kTenthMs;
  }

  frame = renderClockFormat(
      formatIndex, now, settings_.display.clockUse12Hour, tenths,
      format.colonAnimation != ColonAnimation::kBlinking || scheduler_.colonVisible());
  return true;
}

bool DisplayManager::buildCountdownFrame(uint32_t nowMs, bool force,
                                         DisplayFrame& frame) {
  if (!renderElapsed(nowMs, force)) return false;

  const uint8_t formatIndex = activeCountingFormatIndex();
  const DateTime now = rtc_.getNowCached();
  const long secs = static_cast<long>(baseView_.anchor.unixtime()) -
                    static_cast<long>(now.unixtime());

  if (secs <= 0) {
    OverlayState state;
    state.overlay = Overlay::kCountdownComplete;
    copyMessage(state.message, settings_.finalMessage);
    // No expiration set: the countdown has finished, so this stays up until
    // the next mode/config change installs a new view.
    installOverlay(state, nowMs);
    return buildMessageFrame(nowMs, true, frame);
  }

  uint8_t tenths = 0;
  if (displayFormatInfo(kFmtGroupCountdown, formatIndex).refreshRate ==
      RefreshRate::kOneTenth) {
    tenths = (secs > 0) ? (10 - rtc_.msIntoSecond(nowMs) / kTenthMs) % 10 : 0;
  }

  frame = renderCountingFormat(formatIndex, secs, tenths);
  return true;
}

bool DisplayManager::buildCountupFrame(uint32_t nowMs, bool force,
                                       DisplayFrame& frame) {
  if (!renderElapsed(nowMs, force)) return false;

  const uint8_t formatIndex = activeCountingFormatIndex();
  const DateTime now = rtc_.getNowCached();
  const long secs = static_cast<long>(now.unixtime()) -
                    static_cast<long>(baseView_.anchor.unixtime());

  uint8_t tenths = 0;
  if (displayFormatInfo(kFmtGroupCountUp, formatIndex).refreshRate ==
      RefreshRate::kOneTenth) {
    tenths = rtc_.msIntoSecond(nowMs) / kTenthMs;
  }

  frame = renderCountingFormat(formatIndex, secs, tenths);
  return true;
}

bool DisplayManager::buildDemoFrame(uint32_t nowMs, bool force,
                                    DisplayFrame& frame) {
  if (!renderElapsed(nowMs, force)) return false;

  const uint32_t remaining = overlay_.transition.expiresAtMs - nowMs;
  const uint8_t whole  = static_cast<uint8_t>(min<uint32_t>(9, remaining / kSecondMs));
  const uint8_t tenths = static_cast<uint8_t>(min<uint32_t>(9, (remaining % kSecondMs) / kTenthMs));
  frame = renderDemoDisplayFrame(whole, tenths);
  return true;
}

bool DisplayManager::buildMessageFrame(uint32_t nowMs, bool force,
                                       DisplayFrame& frame) {
  const bool blink = overlayBlinks();
  if (blink && scheduler_.toggleBlinkIfDue(nowMs, kMessageBlinkMs)) {
    force = true;
  }

  if (!renderElapsed(nowMs, force)) return false;

  frame = renderMessageDisplayFrame(
      overlay_.message, !blink || scheduler_.blinkOn());
  return true;
}

bool DisplayManager::buildPagedMessageFrame(uint32_t nowMs, bool force,
                                            DisplayFrame& frame) {
  PagedDisplayPayload& paged = overlay_.paged;
  if (paged.pageCount == 0) {
    return false;
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
    return false;
  }

  const DisplayPage& page = paged.pages[paged.currentPage];
  frame = renderPageDisplayFrame(
      page.panels[0], page.panels[1], page.panels[2], scheduler_.blinkOn());
  return true;
}
