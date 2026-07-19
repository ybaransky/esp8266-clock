#pragma once

#include <RTClib.h>
#include "config.h"
#include "schedule.h"
#include "sunset_calculator.h"

class DisplayManager;

// Snapshot of only the configuration fields the Friday schedule consumes.
// Copied from ClockConfig by applySettings() to avoid holding the full config.
struct FridaySettings {
  Mode activeMode = kModeClock;  // Gates ticking to kModeFriday.
  FridayConfig formats{};  // Format index for each Friday phase.
  Location location{};  // Device coordinates and UTC offset for sunset math.
  char sunsetMessage[64] = "";  // Blinked on a live Friday-sunset crossing.
};

// Computes Friday phases, caches weekly sunsets, and pushes view transitions.
class FridayModeController {
 public:
  void applySettings(const ClockConfig& config);
  void resetSunsetCache();
  void tick(const DateTime& now, DisplayManager& displayManager);

 private:
  static DateTime fridayDateFor(const DateTime& now);
  void refreshSunsetCacheIfNeeded(const DateTime& now);

  FridayPhase currentPhase_ = FridayPhase::kNone;  // Last phase applied.
  FridaySettings settings_;  // Friday-relevant settings snapshot.
  DateTime cachedFridayDate_;  // Reference Friday; invalid until first tick.
  DateTime cachedFridaySunset_;  // Cached local Friday sunset.
  DateTime cachedSaturdaySunset_;  // Cached local Saturday sunset.
};
