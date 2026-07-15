#include "trading_mode.h"

#include "display_manager.h"
#include "log.h"

namespace {

constexpr int32_t kBoundaryMessageMs = 5000;
constexpr uint32_t kSecondsPerDay = 86400UL;
constexpr uint32_t kOpenSeconds = 9UL * 3600UL + 30UL * 60UL;
constexpr uint32_t kCloseSeconds = 16UL * 3600UL;

enum class TradingPhase : uint8_t {
  kNone,
  kToOpen,
  kToClose,
};

// Couples the selected Trading phase with the countdown view that represents it.
struct TradingScheduleResult {
  TradingPhase phase = TradingPhase::kToOpen;  // Next market boundary type.
  ViewState view;  // Countdown view anchored at that boundary.
};

bool isTradingWeekday(uint8_t dayOfWeek) {
  return (dayOfWeek >= 1) && (dayOfWeek <= 5);
}

uint8_t daysUntilNextTradingDay(uint8_t dayOfWeek) {
  uint8_t daysAhead = 1;
  uint8_t candidate = (dayOfWeek + daysAhead) % 7;
  while (!isTradingWeekday(candidate)) {
    ++daysAhead;
    candidate = (dayOfWeek + daysAhead) % 7;
  }
  return daysAhead;
}

TradingScheduleResult evaluateTradingSchedule(const DateTime& now,
                                               uint8_t formatIndex) {
  TradingScheduleResult result;
  const DateTime today(now.year(), now.month(), now.day(), 0, 0, 0);
  const uint32_t todayUnix = today.unixtime();
  const uint32_t nowUnix = now.unixtime();
  const uint8_t dayOfWeek = now.dayOfTheWeek();
  const DateTime open(todayUnix + kOpenSeconds);
  const DateTime close(todayUnix + kCloseSeconds);

  if (isTradingWeekday(dayOfWeek) && (nowUnix < open.unixtime())) {
    result.phase = TradingPhase::kToOpen;
    result.view = {View::kCountdown, open, formatIndex};
    return result;
  }

  if (isTradingWeekday(dayOfWeek) && (nowUnix < close.unixtime())) {
    result.phase = TradingPhase::kToClose;
    result.view = {View::kCountdown, close, formatIndex};
    return result;
  }

  const uint8_t daysAhead = daysUntilNextTradingDay(dayOfWeek);
  const DateTime nextOpen(todayUnix + daysAhead * kSecondsPerDay + kOpenSeconds);
  result.phase = TradingPhase::kToOpen;
  result.view = {View::kCountdown, nextOpen, formatIndex};
  return result;
}

const char* tradingPhaseName(TradingPhase phase) {
  switch (phase) {
    case TradingPhase::kToOpen: return "to open";
    case TradingPhase::kToClose: return "to close";
    default: return "none";
  }
}

// Selects the next Trading boundary and announces live open/close crossings.
class TradingModeController {
 public:
  void applySettings(const ClockConfig& config) {
    settings_ = config;
    resetSchedule();
  }

  void resetSchedule() {
    currentPhase_ = TradingPhase::kNone;
    currentTargetUnix_ = 0;
  }

  void tick(const DateTime& now, DisplayManager& displayManager) {
    if (settings_.activeMode != kModeTrading) return;

    const TradingScheduleResult result =
        evaluateTradingSchedule(now, settings_.trading.format);
    const uint32_t targetUnix = result.view.anchor.unixtime();
    if ((result.phase == currentPhase_) &&
        (targetUnix == currentTargetUnix_)) return;

    // Only a live phase crossing announces a boundary. Arriving from kNone
    // (boot, config reload, or a browser time sync) is not the market
    // opening or closing.
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
      displayManager.showInfo(settings_.messages.tradingOpen,
                              kBoundaryMessageMs);
    } else if (crossedClose) {
      displayManager.showInfo(settings_.messages.tradingClose,
                              kBoundaryMessageMs);
    }
  }

 private:
  ClockConfig settings_;  // Configuration snapshot used for schedule evaluation.
  TradingPhase currentPhase_ = TradingPhase::kNone;  // Last phase installed.
  uint32_t currentTargetUnix_ = 0;  // Boundary anchor currently on the display.
};

TradingModeController tradingModeController;

}  // namespace

void tradingModeApplySettings(const ClockConfig& config) {
  tradingModeController.applySettings(config);
}

void tradingModeResetSchedule() {
  tradingModeController.resetSchedule();
}

void tradingModeTick(const DateTime& now, DisplayManager& displayManager) {
  tradingModeController.tick(now, displayManager);
}
