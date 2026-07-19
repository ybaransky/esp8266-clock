#include "trading_mode.h"

#include "display_manager.h"
#include "log.h"

namespace {

constexpr int32_t kBoundaryMessageMs = 5000;
// Couples the selected Trading phase with the countdown view that represents it.
struct TradingScheduleResult {
  TradingPhase phase = TradingPhase::kToOpen;  // Next market boundary type.
  ViewState view;  // Countdown view anchored at that boundary.
};

TradingScheduleResult evaluateTradingSchedule(const DateTime& now,
                                               const TradingConfig& formats) {
  TradingScheduleResult result;
  const DateTime today(now.year(), now.month(), now.day(), 0, 0, 0);
  const TradingBoundary boundary = evaluateTradingBoundary(
      now.unixtime(), today.unixtime(), now.dayOfTheWeek());
  result.phase = boundary.phase;
  result.view = {View::kCountdown, DateTime(boundary.targetUnix), formats.format,
                 formats.formatOver24};
  return result;
}

const char* tradingPhaseName(TradingPhase phase) {
  switch (phase) {
    case TradingPhase::kToOpen: return "to open";
    case TradingPhase::kToClose: return "to close";
    default: return "none";
  }
}

}  // namespace

void TradingModeController::applySettings(const ClockConfig& config) {
  settings_.activeMode = config.activeMode;
  settings_.formats = config.trading;
  strlcpy(settings_.openMessage, config.messages.tradingOpen,
          sizeof(settings_.openMessage));
  strlcpy(settings_.closeMessage, config.messages.tradingClose,
          sizeof(settings_.closeMessage));
  resetSchedule();
}

void TradingModeController::resetSchedule() {
  currentPhase_ = TradingPhase::kNone;
  currentTargetUnix_ = 0;
}

void TradingModeController::tick(const DateTime& now,
                                 DisplayManager& displayManager) {
  if (settings_.activeMode != kModeTrading) return;

  const TradingScheduleResult result =
      evaluateTradingSchedule(now, settings_.formats);
  const uint32_t targetUnix = result.view.anchor.unixtime();
  if ((result.phase == currentPhase_) &&
      (targetUnix == currentTargetUnix_)) return;

  const bool crossedOpen =
      (currentPhase_ == TradingPhase::kToOpen) &&
      (result.phase == TradingPhase::kToClose);
  const bool crossedClose =
      (currentPhase_ == TradingPhase::kToClose) &&
      (result.phase == TradingPhase::kToOpen);

  currentPhase_ = result.phase;
  currentTargetUnix_ = targetUnix;
  displayManager.setView(result.view);
  LOG_PRINTF("trading mode: phase -> %s, target=%04d-%02d-%02d %02d:%02d:%02d",
             tradingPhaseName(result.phase),
             result.view.anchor.year(), result.view.anchor.month(),
             result.view.anchor.day(), result.view.anchor.hour(),
             result.view.anchor.minute(), result.view.anchor.second());

  if (crossedOpen) {
    displayManager.showInfo(settings_.openMessage, kBoundaryMessageMs);
  } else if (crossedClose) {
    displayManager.showInfo(settings_.closeMessage, kBoundaryMessageMs);
  }
}
