#include "scheduled_mode.h"

#include "config_validation.h"
#include "log.h"
#include "sound_player.h"

namespace {

constexpr uint32_t kMaxAnnouncementDelaySeconds = 5;
constexpr int32_t kBoundaryMessageMs = 5000;

void copyCue(BoundaryCue& cue, const char* message, const char* sound) {
  strlcpy(cue.message, message, sizeof(cue.message));
  strlcpy(cue.sound, sound, sizeof(cue.sound));
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
          activeSoundName(config.sound, config.sound.fridaySunset));
  copyCue(openCue_, config.messages.tradingOpen,
          activeSoundName(config.sound, config.sound.tradingOpen));
  copyCue(closeCue_, config.messages.tradingClose,
          activeSoundName(config.sound, config.sound.tradingClose));
  alerts_ = config.sound.boundaryAlert;
  alerts_.enabled = alerts_.enabled && config.sound.enabled;
  reset();
}

void ScheduledModeController::reset() {
  hasPrevious_ = false;
  fridayMidnight_ = 0;
}

void ScheduledModeController::refreshSunsets(const DateTime& now) {
  const DateTime today(now.year(), now.month(), now.day());
  const uint32_t friday = mostRecentFridayMidnight(
      today.unixtime(), now.dayOfTheWeek());
  if (friday == fridayMidnight_) return;
  fridayMidnight_ = friday;
  fridaySunset_ = calculateSunset(DateTime(friday), location_).unixtime();
  saturdaySunset_ = calculateSunset(DateTime(friday + 86400UL), location_).unixtime();
  LOG_PRINTLN("friday mode: refreshed weekly sunsets");
}

ScheduleDecision ScheduledModeController::evaluate(const DateTime& now) {
  if (mode_ == kModeFriday) {
    refreshSunsets(now);
    return evaluateFridaySchedule(now.unixtime(), fridayMidnight_,
                                  fridaySunset_, saturdaySunset_);
  }
  const DateTime today(now.year(), now.month(), now.day());
  return evaluateTradingSchedule(now.unixtime(), today.unixtime(),
                                 now.dayOfTheWeek(), trading_.schedule);
}

ViewState ScheduledModeController::viewFor(const ScheduleDecision& decision) const {
  ViewState view;
  if (decision.view == ScheduleView::kClock) {
    view.formatIndex = friday_.clockFmt;
    return view;
  }
  view.view = View::kCountdown;
  view.anchor = DateTime(decision.next.atLocalSeconds);
  if (mode_ == kModeTrading) {
    view.formatIndex = trading_.format;
    view.longFormatIndex = trading_.formatOver24;
  } else if (decision.next.kind == BoundaryKind::kFridaySunset) {
    view.formatIndex = friday_.toFridaySunsetFmt;
    view.blink = {fridaySunset_ - friday_.blinkBeforeMinutes * 60UL, fridaySunset_};
  } else {
    view.formatIndex = friday_.toSaturdaySunsetFmt;
    view.blink = {fridaySunset_, fridaySunset_ + friday_.blinkAfterMinutes * 60UL};
  }
  return view;
}

ViewState ScheduledModeController::start(const DateTime& now) {
  previous_ = evaluate(now);
  previousTime_ = now.unixtime();
  hasPrevious_ = true;
  return viewFor(previous_);
}

bool ScheduledModeController::crossedBoundary(uint32_t nowLocalSeconds) {
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

const SoundConfig::BoundaryPatternConfig* ScheduledModeController::patternFor(
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
                                    DisplayManager& display, SoundPlayer& sound) {
  if ((mode_ != kModeFriday) && (mode_ != kModeTrading)) return;
  const bool crossed = crossedBoundary(now.unixtime());
  // Evaluate current time last so Friday's cache always describes this view.
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
      sound.play(cue->sound, millis());
    }
  }
  const SoundConfig::BoundaryPatternConfig* pattern = patternFor(decision.next);
  if (pattern == nullptr) {
    sound.cancelBoundaryAlert();
  } else {
    sound.updateBoundaryAlert(decision.next.atLocalSeconds, now.unixtime(),
        secondStartedAtMs, pattern->toneHz, pattern->totalDurationSeconds,
        pattern->startingBeatsHz);
  }
  previous_ = decision;
  previousTime_ = now.unixtime();
  hasPrevious_ = true;
}
