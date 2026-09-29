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
//
// Schedules are rules about local wall-clock time (09:30, Friday midnight,
// sunset on a date), so evaluate() works in local seconds. Everything measured
// as a duration - the countdown anchor, the blink window, the approach alert,
// and whether a boundary was crossed live - is converted to UTC, so a DST
// change between now and a boundary neither adds nor removes an hour.
class ScheduledModeController {
 public:
  void applySettings(const ClockConfig& config);
  void reset();
  // Seeds the boundary tracker and returns the complete initial view silently.
  ViewState start(const DateTime& now, uint32_t nowUtc);
  void tick(const DateTime& now, uint32_t nowUtc, uint32_t secondStartedAtMs,
            DisplayManager& display, BeepPlayer& sound);

 private:
  // Pure with respect to the sunset cache. Callers refresh the cache first.
  ScheduleDecision evaluate(const DateTime& now) const;
  // The only writer of the weekly sunset cache; called once per tick.
  void refreshSunsets(const DateTime& now);
  ViewState viewFor(const ScheduleDecision& decision) const;
  ViewState fridayViewFor(const ScheduleDecision& decision) const;
  ViewState tradingViewFor(const ScheduleDecision& decision) const;
  bool crossedBoundary(uint32_t nowUtc) const;
  // A local wall-clock second on the zone's UTC scale.
  uint32_t toUtc(uint32_t localSeconds) const;
  // Sunset on a local date, using the UTC offset in effect that evening.
  uint32_t sunsetOn(uint32_t localMidnight) const;
  const BoundaryCue* cueFor(BoundaryKind kind) const;
  const BeepPattern* patternFor(
      const ScheduleBoundary& boundary) const;

  Mode m_mode = kModeClock;  // Selects the active schedule, or disables ticking.
  FridayConfig m_friday{};  // Friday presentation settings.
  TradingConfig m_trading{};  // Trading sessions and presentation settings.
  Location m_location{};  // Physical device location; the offset is set per date.
  TimeZoneRule m_zone;  // Converts local boundaries to UTC and sunset dates to offsets.
  BoundaryCue m_fridayCue;  // Friday-sunset announcement.
  BoundaryCue m_openCue;  // Announcement for every Trading open.
  BoundaryCue m_closeCue;  // Announcement for every Trading close.
  SoundConfig::BoundaryAlertConfig m_alerts{};  // Master-resolved approach patterns.
  uint32_t m_fridayMidnight = 0;  // Cache key; zero invalidates the sunsets.
  uint32_t m_fridaySunset = 0;  // Cached local Friday sunset.
  uint32_t m_saturdaySunset = 0;  // Cached local Saturday sunset.
  bool m_hasPrevious = false;  // False after boot, config changes, or time changes.
  ScheduleDecision m_previous;  // Last decision installed as the base view.
  uint32_t m_previousUtc = 0;  // UTC seconds at the previous accepted sample.
};
