#pragma once

#include "config.h"
#include "display_manager.h"
#include "sunset_calculator.h"

class BeepPlayer;

// Couples an event's display message with its optional short beep.
struct BoundaryCue {
  char message[kDisplayMessageLength] = "";  // Message shown for five seconds.
  bool beep = false;  // Master-resolved event-beep enable.
};

// Applies Friday/Trading decisions, caches sunsets, and tracks one live boundary.
class ScheduledModeController {
 public:
  void applySettings(const ClockConfig& config);
  void reset();
  // Seeds the boundary tracker and returns the complete initial view silently.
  ViewState start(const DateTime& now);
  void tick(const DateTime& now, uint32_t secondStartedAtMs,
            DisplayManager& display, BeepPlayer& sound);

 private:
  // Pure with respect to the sunset cache. Callers refresh the cache first.
  ScheduleDecision evaluate(const DateTime& now) const;
  // The only writer of the weekly sunset cache; called once per tick.
  void refreshSunsets(const DateTime& now);
  ViewState viewFor(const ScheduleDecision& decision) const;
  ViewState fridayViewFor(const ScheduleDecision& decision) const;
  ViewState tradingViewFor(const ScheduleDecision& decision) const;
  bool crossedBoundary(uint32_t nowLocalSeconds) const;
  const BoundaryCue* cueFor(BoundaryKind kind) const;
  const BeepPattern* patternFor(
      const ScheduleBoundary& boundary) const;

  Mode mode_ = kModeClock;  // Selects the active schedule, or disables ticking.
  FridayConfig friday_{};  // Friday presentation settings.
  TradingConfig trading_{};  // Trading sessions and presentation settings.
  Location location_{};  // Physical device location and numeric UTC offset.
  BoundaryCue fridayCue_;  // Friday-sunset announcement.
  BoundaryCue openCue_;  // Announcement for every Trading open.
  BoundaryCue closeCue_;  // Announcement for every Trading close.
  SoundConfig::BoundaryAlertConfig alerts_{};  // Master-resolved approach patterns.
  uint32_t fridayMidnight_ = 0;  // Cache key; zero invalidates the sunsets.
  uint32_t fridaySunset_ = 0;  // Cached local Friday sunset.
  uint32_t saturdaySunset_ = 0;  // Cached local Saturday sunset.
  bool hasPrevious_ = false;  // False after boot, config changes, or time changes.
  ScheduleDecision previous_;  // Last decision installed as the base view.
  uint32_t previousTime_ = 0;  // Local seconds at the previous accepted sample.
};
