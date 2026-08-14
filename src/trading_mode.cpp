#include "trading_mode.h"

#include "config_validation.h"
#include "display_manager.h"
#include "log.h"
#include "sound_player.h"

namespace {

constexpr int32_t kBoundaryMessageMs = 5000;
// Couples the selected Trading phase with the countdown view that represents it.
struct TradingScheduleResult {
  TradingPhase phase = TradingPhase::kToOpen;  // Next market boundary type.
  ViewState view;  // Countdown view anchored at that boundary.
};

TradingScheduleResult evaluateTradingSchedule(const DateTime& now,
                                               const TradingConfig& config) {
  TradingScheduleResult result;
  const DateTime today(now.year(), now.month(), now.day(), 0, 0, 0);
  const TradingBoundary boundary = evaluateTradingBoundary(
      now.unixtime(), today.unixtime(), now.dayOfTheWeek(), config.schedule);
  result.phase = boundary.phase;
  result.view = {View::kCountdown, DateTime(boundary.targetUnix), config.format,
                 config.formatOver24};
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
  settings_.config = config.trading;
  strlcpy(settings_.openMessage, config.messages.tradingOpen,
          sizeof(settings_.openMessage));
  strlcpy(settings_.closeMessage, config.messages.tradingClose,
          sizeof(settings_.closeMessage));
  strlcpy(settings_.openSound,
          activeSoundName(config.sound, config.sound.tradingOpen),
          sizeof(settings_.openSound));
  strlcpy(settings_.closeSound,
          activeSoundName(config.sound, config.sound.tradingClose),
          sizeof(settings_.closeSound));
  resetSchedule();
}

void TradingModeController::resetSchedule() {
  phaseTracker_.reset();
}

void TradingModeController::tick(const DateTime& now, ModeOutputs& outputs) {
  if (settings_.activeMode != kModeTrading) return;

  const TradingScheduleResult result =
      evaluateTradingSchedule(now, settings_.config);
  const uint32_t targetUnix = result.view.anchor.unixtime();
  if (!phaseTracker_.hasChanged(result.phase, targetUnix)) return;

  const TradingPhase previousPhase = phaseTracker_.phase();
  phaseTracker_.accept(result.phase, targetUnix);

  const bool crossedOpen =
      (previousPhase == TradingPhase::kToOpen) &&
      (result.phase == TradingPhase::kToClose);
  const bool crossedClose =
      (previousPhase == TradingPhase::kToClose) &&
      (result.phase == TradingPhase::kToOpen);

  outputs.display.setView(result.view);
  LOG_PRINTF("trading mode: phase -> %s, target=%04d-%02d-%02d %02d:%02d:%02d",
             tradingPhaseName(result.phase),
             result.view.anchor.year(), result.view.anchor.month(),
             result.view.anchor.day(), result.view.anchor.hour(),
             result.view.anchor.minute(), result.view.anchor.second());

  // A crossing out of kNone - boot, config reload, a browser time sync - is
  // neither of these, so those events stay silent exactly as they stay
  // message-free.
  if (crossedOpen) {
    outputs.display.showInfo(settings_.openMessage, kBoundaryMessageMs);
    outputs.sound.play(settings_.openSound, millis());
  } else if (crossedClose) {
    outputs.display.showInfo(settings_.closeMessage, kBoundaryMessageMs);
    outputs.sound.play(settings_.closeSound, millis());
  }
}
