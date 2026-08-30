#pragma once

#include <RTClib.h>

#include "config.h"
#include "mode_outputs.h"
#include "phase_tracker.h"
#include "schedule.h"

// Snapshot of only the configuration fields the Trading schedule consumes.
// Copied from ClockConfig by applySettings() to avoid holding the full config.
struct TradingSettings {
  Mode activeMode = kModeClock;  // Gates ticking to kModeTrading.
  TradingConfig config{};  // Trading formats and session schedule.
  char openMessage[64] = "";  // Blinked when any session starts live.
  char closeMessage[64] = "";  // Blinked when any session stops live.
  // Played alongside those messages. Already resolved against the master sound
  // switch by applySettings(), so an empty name is the only "stay silent" test
  // the tick path needs.
  char openSound[kSoundNameLength] = "";
  char closeSound[kSoundNameLength] = "";
  bool boundaryAlertEnabled = false;  // Master-resolved generated-alert switch.
  SoundConfig::BoundaryPatternConfig boundary1{};  // First daily open pattern.
  SoundConfig::BoundaryPatternConfig boundary2{};  // Last daily close pattern.
};

// Selects the next Trading boundary and announces live open/close crossings.
class TradingModeController {
 public:
  void applySettings(const ClockConfig& config);
  void resetSchedule();
  void tick(const DateTime& now, uint32_t secondStartedAtMs,
            ModeOutputs& outputs);

 private:
  TradingSettings settings_;  // Trading-relevant settings snapshot.
  PhaseTracker<TradingPhase> phaseTracker_;  // Last phase+target installed.
};
