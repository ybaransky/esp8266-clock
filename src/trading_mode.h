#pragma once

#include <RTClib.h>

#include "config.h"
#include "phase_tracker.h"
#include "schedule.h"

class DisplayManager;

// Snapshot of only the configuration fields the Trading schedule consumes.
// Copied from ClockConfig by applySettings() to avoid holding the full config.
struct TradingSettings {
  Mode activeMode = kModeClock;  // Gates ticking to kModeTrading.
  TradingConfig config{};  // Trading formats and session schedule.
  char openMessage[64] = "";  // Blinked when any session starts live.
  char closeMessage[64] = "";  // Blinked when any session stops live.
};

// Selects the next Trading boundary and announces live open/close crossings.
class TradingModeController {
 public:
  void applySettings(const ClockConfig& config);
  void resetSchedule();
  void tick(const DateTime& now, DisplayManager& displayManager);

 private:
  TradingSettings settings_;  // Trading-relevant settings snapshot.
  PhaseTracker<TradingPhase> phaseTracker_;  // Last phase+target installed.
};
