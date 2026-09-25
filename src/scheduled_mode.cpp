#include "scheduled_mode.h"

#include "config_validation.h"
#include "log.h"
#include "beep_player.h"

namespace {

constexpr uint32_t kMaxAnnouncementDelaySeconds = 5;
constexpr int32_t kBoundaryMessageMs = 5000;

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
  mode_ = config.activeMode;
  friday_ = config.friday;
  trading_ = config.trading;
  location_ = {config.locations.device.latitude, config.locations.device.longitude,
               config.timezone.utcOffsetMinutes};
  copyCue(fridayCue_, config.messages.fridaySunset,
          config.sound.enabled && config.sound.fridaySunsetBeep);
  copyCue(openCue_, config.messages.tradingOpen,
          config.sound.enabled && config.sound.tradingOpenBeep);
  copyCue(closeCue_, config.messages.tradingClose,
          config.sound.enabled && config.sound.tradingCloseBeep);
  alerts_ = config.sound.boundaryAlert;
  alerts_.enabled = alerts_.enabled && config.sound.enabled;
  reset();
}

void ScheduledModeController::reset() {
  hasPrevious_ = false;
  fridayMidnight_ = 0;
}

void ScheduledModeController::refreshSunsets(const DateTime& now) {
  // Only Friday mode has sunsets. Callers refresh unconditionally so the cache
  // is always current before an evaluation; returning early here keeps Trading
  // mode from doing Friday's weekly bookkeeping on every RTC second.
  if (mode_ != kModeFriday) return;

  const DateTime today(now.year(), now.month(), now.day());
  const uint32_t friday = mostRecentFridayMidnight(
      today.unixtime(), now.dayOfTheWeek());
  if (friday == fridayMidnight_) return;
  fridayMidnight_ = friday;
  fridaySunset_ = calculateSunset(DateTime(friday), location_).unixtime();
  saturdaySunset_ = calculateSunset(DateTime(friday + 86400UL), location_).unixtime();
  LOG_PRINTLN("friday mode: refreshed weekly sunsets");
}

// Pure with respect to the sunset cache: callers refresh it once via
// refreshSunsets() before evaluating. That is what lets crossedBoundary()
// evaluate a past boundary without moving the cache out from under the
// decision being installed - previously the two had to be sequenced carefully
// and a comment had to explain the ordering.
ScheduleDecision ScheduledModeController::evaluate(const DateTime& now) const {
  if (mode_ == kModeFriday) {
    return evaluateFridaySchedule(now.unixtime(), fridayMidnight_,
                                  fridaySunset_, saturdaySunset_);
  }
  const DateTime today(now.year(), now.month(), now.day());
  return evaluateTradingSchedule(now.unixtime(), today.unixtime(),
                                 now.dayOfTheWeek(), trading_.schedule);
}

// Friday presentation: a clock phase, or one of two countdowns, each with the
// blink window that brackets Friday sunset.
ViewState ScheduledModeController::fridayViewFor(
    const ScheduleDecision& decision) const {
  ViewState view;
  if (decision.view == ScheduleView::kClock) {
    view.formatIndex = friday_.clockFmt;
    return view;
  }
  view.view = View::kCountdown;
  view.anchor = DateTime(decision.next.atLocalSeconds);
  if (decision.next.kind == BoundaryKind::kFridaySunset) {
    view.formatIndex = friday_.toFridaySunsetFmt;
    view.blink = {fridaySunset_ - friday_.blinkBeforeMinutes * 60UL, fridaySunset_};
  } else {
    view.formatIndex = friday_.toSaturdaySunsetFmt;
    view.blink = {fridaySunset_, fridaySunset_ + friday_.blinkAfterMinutes * 60UL};
  }
  return view;
}

// Trading presentation. evaluateTradingSchedule() only ever returns a
// countdown (test_trading_always_counts_down pins that down), so there is no
// clock branch here to accidentally borrow Friday's clock format - which is
// what the single combined viewFor() used to do for any kClock decision.
ViewState ScheduledModeController::tradingViewFor(
    const ScheduleDecision& decision) const {
  ViewState view;
  view.view = View::kCountdown;
  view.anchor = DateTime(decision.next.atLocalSeconds);
  view.formatIndex = trading_.format;
  view.longFormatIndex = trading_.formatOver24;
  return view;
}

ViewState ScheduledModeController::viewFor(const ScheduleDecision& decision) const {
  return (mode_ == kModeTrading) ? tradingViewFor(decision)
                                 : fridayViewFor(decision);
}

ViewState ScheduledModeController::start(const DateTime& now) {
  refreshSunsets(now);
  previous_ = evaluate(now);
  previousTime_ = now.unixtime();
  hasPrevious_ = true;
  return viewFor(previous_);
}

bool ScheduledModeController::crossedBoundary(uint32_t nowLocalSeconds) const {
  if (!hasPrevious_ || (nowLocalSeconds < previousTime_)) return false;
  const uint32_t boundary = previous_.next.atLocalSeconds;
  if ((previousTime_ >= boundary) || (nowLocalSeconds < boundary) ||
      (nowLocalSeconds - boundary > kMaxAnnouncementDelaySeconds)) return false;

  // A stall spanning multiple boundaries installs today's state silently.
  const ScheduleDecision afterBoundary = evaluate(DateTime(boundary));
  return afterBoundary.next.atLocalSeconds > nowLocalSeconds;
}

const BoundaryCue* ScheduledModeController::cueFor(BoundaryKind kind) const {
  switch (kind) {
    case BoundaryKind::kFridaySunset: return &fridayCue_;
    case BoundaryKind::kTradingOpen: return &openCue_;
    case BoundaryKind::kTradingClose: return &closeCue_;
    default: return nullptr;
  }
}

const BeepPattern* ScheduledModeController::patternFor(
    const ScheduleBoundary& boundary) const {
  if (!alerts_.enabled) return nullptr;
  if ((boundary.kind == BoundaryKind::kFridaySunset) ||
      ((boundary.kind == BoundaryKind::kTradingOpen) && (boundary.sessionIndex == 0))) {
    return &alerts_.boundary1;
  }
  if ((boundary.kind == BoundaryKind::kSaturdaySunset) ||
      ((boundary.kind == BoundaryKind::kTradingClose) &&
       (boundary.sessionIndex + 1 == trading_.schedule.intervalCount))) {
    return &alerts_.boundary2;
  }
  return nullptr;
}

void ScheduledModeController::tick(const DateTime& now, uint32_t secondStartedAtMs,
                                    DisplayManager& display, BeepPlayer& sound) {
  if ((mode_ != kModeFriday) && (mode_ != kModeTrading)) return;
  // One cache refresh for the whole tick. Both evaluations below then read a
  // cache that describes this instant, in either order.
  refreshSunsets(now);
  const bool crossed = crossedBoundary(now.unixtime());
  const ScheduleDecision decision = evaluate(now);
  if (!hasPrevious_ || !sameScheduleDecision(previous_, decision)) {
    display.setView(viewFor(decision));
    const DateTime target(decision.next.atLocalSeconds);
    LOG_PRINTF("schedule: next %s, session=%u, at=%04u-%02u-%02u %02u:%02u:%02u",
               boundaryName(decision.next.kind), decision.next.sessionIndex + 1,
               target.year(), target.month(), target.day(), target.hour(),
               target.minute(), target.second());
  }
  if (crossed) {
    const BoundaryCue* cue = cueFor(previous_.next.kind);
    if (cue != nullptr) {
      display.showInfo(cue->message, kBoundaryMessageMs);
      if (cue->beep) {
        sound.beep(previous_.next.kind == BoundaryKind::kTradingClose ? 1320 : 880,
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
  previous_ = decision;
  previousTime_ = now.unixtime();
  hasPrevious_ = true;
}
