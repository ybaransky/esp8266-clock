#pragma once

#include <RTClib.h>
#include "config.h"
#include "mode_outputs.h"
#include "phase_tracker.h"
#include "schedule.h"
#include "sunset_calculator.h"

// Snapshot of only the configuration fields the Friday schedule consumes.
// Copied from ClockConfig by applySettings() to avoid holding the full config.
struct FridaySettings {
  Mode activeMode = kModeClock;  // Gates ticking to kModeFriday.
  FridayConfig formats{};  // Per-phase format indexes and the sunset blink windows.
  Location location{};  // Device coordinates and UTC offset for sunset math.
  char sunsetMessage[64] = "";  // Blinked on a live Friday-sunset crossing.
  // Played alongside that message. Already resolved against the master sound
  // switch by applySettings(), so an empty name is the only "stay silent" test
  // the tick path needs.
  char sunsetSound[kSoundNameLength] = "";
};

// Computes Friday phases, caches weekly sunsets, and pushes view transitions.
class FridayModeController {
 public:
  void applySettings(const ClockConfig& config);
  void resetSchedule();
  void tick(const DateTime& now, ModeOutputs& outputs);

 private:
  static DateTime fridayDateFor(const DateTime& now);
  void refreshSunsetCacheIfNeeded(const DateTime& now);

  PhaseTracker<FridayPhase> phaseTracker_;  // Last phase+target applied.
  FridaySettings settings_;  // Friday-relevant settings snapshot.
  DateTime cachedFridayDate_;  // Reference Friday; invalid until first tick.
  DateTime cachedFridaySunset_;  // Cached local Friday sunset.
  DateTime cachedSaturdaySunset_;  // Cached local Saturday sunset.
};
