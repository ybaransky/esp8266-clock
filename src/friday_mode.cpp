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
    if (settings_.activeMode != kPersistentFriday) return;

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

  // Returns midnight of the reference Friday for the current week.
  // On Thursday, returns tomorrow (upcoming Friday) so the countdown starts at Thursday midnight.
  // On all other days, returns the most recent past Friday (or today if Friday).
  static DateTime fridayDateFor(const DateTime& now) {
    // dayOfTheWeek(): 0=Sun, 1=Mon, 2=Tue, 3=Wed, 4=Thu, 5=Fri, 6=Sat
    const uint8_t dow = now.dayOfTheWeek();
    const uint32_t todayMidnight =
        DateTime(now.year(), now.month(), now.day(), 0, 0, 0).unixtime();
    if (dow == 4) {
      return DateTime(todayMidnight + 86400UL);  // Thursday: reference is tomorrow (Friday)
    }
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

  Phase computePhase(const DateTime& now) const {
    const uint32_t nowUnix          = now.unixtime();
    const uint32_t thursdayMidnight = cachedFridayDate_.unixtime() - 86400UL;

    if (nowUnix < thursdayMidnight)                 return Phase::kClock;
    if (nowUnix < cachedFridaySunset_.unixtime())   return Phase::kToFridaySunset;
    if (nowUnix < cachedSaturdaySunset_.unixtime()) return Phase::kToSaturdaySunset;
    return Phase::kClock;
  }

  void applyPhase(Phase phase) {
    DisplayState state;
    switch (phase) {
      case Phase::kToFridaySunset:
        state.behavior = DisplayBehavior::kCountdown;
        state.payload.countdown.endTime     = cachedFridaySunset_;
        state.payload.countdown.formatIndex = settings_.fridayToFridaySunsetFmt;
        break;
      case Phase::kToSaturdaySunset:
        state.behavior = DisplayBehavior::kCountdown;
        state.payload.countdown.endTime     = cachedSaturdaySunset_;
        state.payload.countdown.formatIndex = settings_.fridayToSatSunsetFmt;
        break;
      default:
        state.behavior = DisplayBehavior::kClock;
        state.payload.clock.formatIndex = settings_.fridayClockFmt;
        break;
    }
    displayManager.setDefaultState(state);
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
