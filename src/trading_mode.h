#pragma once

#include <RTClib.h>

#include "config.h"
#include "schedule.h"

class DisplayManager;

// Snapshot of only the configuration fields the Trading schedule consumes.
// Copied from ClockConfig by applySettings() to avoid holding the full config.
struct TradingSettings {
  Mode activeMode = kModeClock;  // Gates ticking to kModeTrading.
  TradingConfig formats{};  // Countdown formats for the boundary phases.
  char openMessage[64] = "";  // Blinked on a live 09:30 open crossing.
  char closeMessage[64] = "";  // Blinked on a live 16:00 close crossing.
};

// Selects the next Trading boundary and announces live open/close crossings.
class TradingModeController {
 public:
  void applySettings(const ClockConfig& config);
  void resetSchedule();
  void tick(const DateTime& now, DisplayManager& displayManager);

 private:
  TradingSettings settings_;  // Trading-relevant settings snapshot.
  TradingPhase currentPhase_ = TradingPhase::kNone;  // Last phase installed.
  uint32_t currentTargetUnix_ = 0;  // Boundary currently on the display.
};
