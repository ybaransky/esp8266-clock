#include "friday_mode.h"

#include "display_manager.h"
#include "log.h"
#include "sunset_calculator.h"

namespace {

class FridayModeController {
 public:
  void applySettings(const ClockConfig& config) {
    settings_ = config;
    currentPhase_ = Phase::kNone;
    cachedFridayDate_ = DateTime();  // invalidate cache
  }

  void resetSunsetCache() {
    cachedFridayDate_ = DateTime();
  }

  void tick(const DateTime& now) {
    if (settings_.activeMode != kModeFriday) return;

    refreshSunsetCacheIfNeeded(now);
    const Phase phase = computePhase(now);

    if (phase == currentPhase_) return;
    LOG_PRINTF("friday mode: phase -> %s\n", phaseName(phase));
    currentPhase_ = phase;
    applyPhase(phase);
  }

 private:
  enum class Phase : uint8_t { kNone, kClock, kToFridaySunset, kToSaturdaySunset };

  static const char* phaseName(Phase phase) {
    switch (phase) {
      case Phase::kToFridaySunset:   return "to friday sunset";
      case Phase::kToSaturdaySunset: return "to saturday sunset";
      default:                       return "clock";
    }
  }

  // Returns midnight of the most recent Friday (or today if Friday).
  // This stays constant from Saturday through the following Thursday, so the
  // Clock/DOW phase covers all of that span; it only advances once we reach
  // the next Friday, which is when the countdown phases take over.
  static DateTime fridayDateFor(const DateTime& now) {
    // dayOfTheWeek(): 0=Sun, 1=Mon, 2=Tue, 3=Wed, 4=Thu, 5=Fri, 6=Sat
    const uint8_t dow = now.dayOfTheWeek();
    const uint32_t todayMidnight =
        DateTime(now.year(), now.month(), now.day(), 0, 0, 0).unixtime();
    const uint8_t daysSinceFriday = (dow >= 5) ? (dow - 5) : (dow + 2);
    return DateTime(todayMidnight - daysSinceFriday * 86400UL);
  }

  void refreshSunsetCacheIfNeeded(const DateTime& now) {
    const DateTime fridayDate = fridayDateFor(now);
    if (fridayDate.unixtime() == cachedFridayDate_.unixtime()) return;

    cachedFridayDate_ = fridayDate;
    const DateTime saturdayDate(fridayDate.unixtime() + 86400UL);
    const Location loc{settings_.location.latitude,
                       settings_.location.longitude,
                       settings_.utcOffsetMinutes};
    cachedFridaySunset_   = calculateSunset(fridayDate,   loc);
    cachedSaturdaySunset_ = calculateSunset(saturdayDate, loc);
    LOG_PRINTF("friday mode: recomputed sunsets for %04d-%02d-%02d\n",
               fridayDate.year(), fridayDate.month(), fridayDate.day());
  }

  // cachedFridayDate_ is never in the future relative to `now` (see
  // fridayDateFor), so it never needs to be checked directly here: Sat-Thu
  // fall through to the default kClock below because both cached sunsets are
  // stale (last week's, already in the past) until the Friday cache refresh.
  Phase computePhase(const DateTime& now) const {
    const uint32_t nowUnix = now.unixtime();

    if (nowUnix < cachedFridaySunset_.unixtime())   return Phase::kToFridaySunset;
    if (nowUnix < cachedSaturdaySunset_.unixtime()) return Phase::kToSaturdaySunset;
    return Phase::kClock;
  }

  void applyPhase(Phase phase) {
    ViewState state;
    switch (phase) {
      case Phase::kToFridaySunset:
        state.view = View::kCountdown;
        state.anchor      = cachedFridaySunset_;
        state.formatIndex = settings_.fridayToFridaySunsetFmt;
        break;
      case Phase::kToSaturdaySunset:
        state.view = View::kCountdown;
        state.anchor      = cachedSaturdaySunset_;
        state.formatIndex = settings_.fridayToSatSunsetFmt;
        break;
      default:
        state.view = View::kClock;
        state.formatIndex = settings_.fridayClockFmt;
        break;
    }
    displayManager.setView(state);
  }

  Phase    currentPhase_       = Phase::kNone;
  ClockConfig settings_;
  DateTime cachedFridayDate_;     // Midnight of the reference Friday; invalid until first tick.
  DateTime cachedFridaySunset_;   // Cached local sunset for that Friday.
  DateTime cachedSaturdaySunset_; // Cached local sunset for that Saturday.
};

FridayModeController fridayModeController;

}  // namespace

void fridayModeApplySettings(const ClockConfig& config) {
  fridayModeController.applySettings(config);
}

void fridayModeResetSunsetCache() {
  fridayModeController.resetSunsetCache();
}

void fridayModeTick(const DateTime& now) {
  fridayModeController.tick(now);
}
