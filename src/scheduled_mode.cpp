#include "scheduled_mode.h"

#include "config_validation.h"
#include "log.h"
#include "beep_player.h"

namespace {

constexpr uint32_t kMaxAnnouncementDelaySeconds = 5;  // Later than this, a crossing is installed silently.
constexpr int32_t kBoundaryMessageMs = 5000;          // How long a boundary message blinks.

void copyCue(BoundaryCue& cue, const char* message, bool beep) {
  strlcpy(cue.message, message, sizeof(cue.message));
  cue.beep = beep;
}

const char* boundaryName(BoundaryKind kind) {
  switch (kind) {
    case BoundaryKind::kFridayMidnight: return "friday midnight";
    case BoundaryKind::kFridaySunset: return "friday sunset";
    case BoundaryKind::kSaturdaySunset: return "saturday sunset";
    case BoundaryKind::kTradingOpen: return "trading open";
    case BoundaryKind::kTradingClose: return "trading close";
  }
  return "?";
}

}  // namespace

void ScheduledModeController::applySettings(const ClockConfig& config) {
  m_mode = config.activeMode;
  m_friday = config.friday;
  m_trading = config.trading;
  m_location = {config.locations.device.latitude, config.locations.device.longitude,
               config.timezone.utcOffsetMinutes};
  copyCue(m_fridayCue, config.messages.fridaySunset,
          config.sound.enabled && config.sound.fridaySunsetBeep);
  copyCue(m_openCue, config.messages.tradingOpen,
          config.sound.enabled && config.sound.tradingOpenBeep);
  copyCue(m_closeCue, config.messages.tradingClose,
          config.sound.enabled && config.sound.tradingCloseBeep);
  m_alerts = config.sound.boundaryAlert;
  m_alerts.enabled = m_alerts.enabled && config.sound.enabled;
  reset();
}

void ScheduledModeController::reset() {
  m_hasPrevious = false;
  m_fridayMidnight = 0;
}

void ScheduledModeController::refreshSunsets(const DateTime& now) {
  // Only Friday mode has sunsets. Callers refresh unconditionally so the cache
  // is always current before an evaluation; returning early here keeps Trading
  // mode from doing Friday's weekly bookkeeping on every RTC second.
  if (m_mode != kModeFriday) return;

  const DateTime today(now.year(), now.month(), now.day());
  const uint32_t friday = mostRecentFridayMidnight(
      today.unixtime(), now.dayOfTheWeek());
  if (friday == m_fridayMidnight) return;
  m_fridayMidnight = friday;
  m_fridaySunset = calculateSunset(DateTime(friday), m_location).unixtime();
  m_saturdaySunset = calculateSunset(DateTime(friday + 86400UL), m_location).unixtime();
  LOG_PRINTLN("friday mode: refreshed weekly sunsets");
}

// Pure with respect to the sunset cache: callers refresh it once via
// refreshSunsets() before evaluating. That is what lets crossedBoundary()
// evaluate a past boundary without moving the cache out from under the
// decision being installed, with no statement ordering to get right.
ScheduleDecision ScheduledModeController::evaluate(const DateTime& now) const {
  if (m_mode == kModeFriday) {
    return evaluateFridaySchedule(now.unixtime(), m_fridayMidnight,
                                  m_fridaySunset, m_saturdaySunset);
  }
  const DateTime today(now.year(), now.month(), now.day());
  return evaluateTradingSchedule(now.unixtime(), today.unixtime(),
                                 now.dayOfTheWeek(), m_trading.schedule);
}

// Friday presentation: a clock phase, or one of two countdowns, each with the
// blink window that brackets Friday sunset.
ViewState ScheduledModeController::fridayViewFor(
    const ScheduleDecision& decision) const {
  ViewState view;
  if (decision.view == ScheduleView::kClock) {
    view.formatIndex = m_friday.clockFormat;
    return view;
  }
  view.view = View::kCountdown;
  view.anchor = DateTime(decision.next.atLocalSeconds);
  if (decision.next.kind == BoundaryKind::kFridaySunset) {
    view.formatIndex = m_friday.toFridaySunsetFormat;
    view.blink = {m_fridaySunset - m_friday.blinkBeforeMinutes * 60UL, m_fridaySunset};
  } else {
    view.formatIndex = m_friday.toSaturdaySunsetFormat;
    view.blink = {m_fridaySunset, m_fridaySunset + m_friday.blinkAfterMinutes * 60UL};
  }
  return view;
}

// Trading presentation. evaluateTradingSchedule() only ever returns a
// countdown (test_trading_always_counts_down pins that down), so there is no
// clock branch here, and a Trading decision can never borrow Friday's clock
// format.
ViewState ScheduledModeController::tradingViewFor(
    const ScheduleDecision& decision) const {
  ViewState view;
  view.view = View::kCountdown;
  view.anchor = DateTime(decision.next.atLocalSeconds);
  view.formatIndex = m_trading.format;
  view.longFormatIndex = m_trading.formatOver24;
  return view;
}

ViewState ScheduledModeController::viewFor(const ScheduleDecision& decision) const {
  return (m_mode == kModeTrading) ? tradingViewFor(decision)
                                 : fridayViewFor(decision);
}

ViewState ScheduledModeController::start(const DateTime& now) {
  refreshSunsets(now);
  m_previous = evaluate(now);
  m_previousTime = now.unixtime();
  m_hasPrevious = true;
  return viewFor(m_previous);
}

bool ScheduledModeController::crossedBoundary(uint32_t nowLocalSeconds) const {
  if (!m_hasPrevious || (nowLocalSeconds < m_previousTime)) return false;
  const uint32_t boundary = m_previous.next.atLocalSeconds;
  if ((m_previousTime >= boundary) || (nowLocalSeconds < boundary) ||
      (nowLocalSeconds - boundary > kMaxAnnouncementDelaySeconds)) return false;

  // A stall spanning multiple boundaries installs today's state silently.
  const ScheduleDecision afterBoundary = evaluate(DateTime(boundary));
  return afterBoundary.next.atLocalSeconds > nowLocalSeconds;
}

const BoundaryCue* ScheduledModeController::cueFor(BoundaryKind kind) const {
  switch (kind) {
    case BoundaryKind::kFridaySunset: return &m_fridayCue;
    case BoundaryKind::kTradingOpen: return &m_openCue;
    case BoundaryKind::kTradingClose: return &m_closeCue;
    default: return nullptr;
  }
}

const BeepPattern* ScheduledModeController::patternFor(
    const ScheduleBoundary& boundary) const {
  if (!m_alerts.enabled) return nullptr;
  if ((boundary.kind == BoundaryKind::kFridaySunset) ||
      ((boundary.kind == BoundaryKind::kTradingOpen) && (boundary.sessionIndex == 0))) {
    return &m_alerts.boundary1;
  }
  if ((boundary.kind == BoundaryKind::kSaturdaySunset) ||
      ((boundary.kind == BoundaryKind::kTradingClose) &&
       (boundary.sessionIndex + 1 == m_trading.schedule.intervalCount))) {
    return &m_alerts.boundary2;
  }
  return nullptr;
}

void ScheduledModeController::tick(const DateTime& now, uint32_t secondStartedAtMs,
                                    DisplayManager& display, BeepPlayer& sound) {
  if ((m_mode != kModeFriday) && (m_mode != kModeTrading)) return;
  // One cache refresh for the whole tick. Both evaluations below then read a
  // cache that describes this instant, in either order.
  refreshSunsets(now);
  const bool crossed = crossedBoundary(now.unixtime());
  const ScheduleDecision decision = evaluate(now);
  if (!m_hasPrevious || !sameScheduleDecision(m_previous, decision)) {
    display.setView(viewFor(decision));
    const DateTime target(decision.next.atLocalSeconds);
    LOG_PRINTF("schedule: next %s, session=%u, at=%04u-%02u-%02u %02u:%02u:%02u",
               boundaryName(decision.next.kind), decision.next.sessionIndex + 1,
               target.year(), target.month(), target.day(), target.hour(),
               target.minute(), target.second());
  }
  if (crossed) {
    const BoundaryCue* cue = cueFor(m_previous.next.kind);
    if (cue != nullptr) {
      display.showInfo(cue->message, kBoundaryMessageMs);
      if (cue->beep) {
        sound.beep(m_previous.next.kind == BoundaryKind::kTradingClose ? 1320 : 880,
                   millis());
      }
    }
  }
  const BeepPattern* pattern = patternFor(decision.next);
  if (pattern == nullptr) {
    sound.cancelBoundaryAlert();
  } else {
    sound.updateBoundaryAlert(decision.next.atLocalSeconds, now.unixtime(),
        secondStartedAtMs, *pattern);
  }
  m_previous = decision;
  m_previousTime = now.unixtime();
  m_hasPrevious = true;
}
