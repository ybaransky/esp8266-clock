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
  }

  void tick(const DateTime& now) {
    if (settings_.activeMode != kPersistentFriday) return;

    DateTime fridaySunset, saturdaySunset;
    const Phase phase = computePhase(now, &fridaySunset, &saturdaySunset);

    if (phase == currentPhase_) return;
    LOG_PRINTF("friday mode: phase -> %s\n", phaseName(phase));
    currentPhase_ = phase;
    applyPhase(phase, fridaySunset, saturdaySunset);
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

  // Returns the active phase and computes the sunset targets for this week.
  Phase computePhase(const DateTime& now,
                     DateTime* fridaySunset,
                     DateTime* saturdaySunset) const {
    // dayOfTheWeek(): 0=Sun, 1=Mon, 2=Tue, 3=Wed, 4=Thu, 5=Fri, 6=Sat
    const uint8_t dow = now.dayOfTheWeek();
    const uint8_t daysSinceFriday = (dow >= 5) ? (dow - 5) : (dow + 2);

    const uint32_t todayMidnightUnix =
        DateTime(now.year(), now.month(), now.day(), 0, 0, 0).unixtime();
    const DateTime fridayDate(todayMidnightUnix - daysSinceFriday * 86400UL);
    const DateTime saturdayDate(fridayDate.unixtime() + 86400UL);

    const Location loc{settings_.location.latitude,
                       settings_.location.longitude,
                       settings_.utcOffsetMinutes};
    *fridaySunset   = calculateSunset(fridayDate,   loc);
    *saturdaySunset = calculateSunset(saturdayDate, loc);

    const uint32_t nowUnix         = now.unixtime();
    const uint32_t fridayMidnight  = fridayDate.unixtime();

    if (nowUnix < fridayMidnight)             return Phase::kClock;
    if (nowUnix < fridaySunset->unixtime())   return Phase::kToFridaySunset;
    if (nowUnix < saturdaySunset->unixtime()) return Phase::kToSaturdaySunset;
    return Phase::kClock;
  }

  void applyPhase(Phase phase,
                  const DateTime& fridaySunset,
                  const DateTime& saturdaySunset) {
    DisplayState state;
    switch (phase) {
      case Phase::kToFridaySunset:
        state.behavior = DisplayBehavior::kCountdown;
        state.payload.countdown.endTime     = fridaySunset;
        state.payload.countdown.formatIndex = settings_.fridayToFridaySunsetFmt;
        break;
      case Phase::kToSaturdaySunset:
        state.behavior = DisplayBehavior::kCountdown;
        state.payload.countdown.endTime     = saturdaySunset;
        state.payload.countdown.formatIndex = settings_.fridayToSatSunsetFmt;
        break;
      default:
        state.behavior = DisplayBehavior::kClock;
        state.payload.clock.formatIndex = settings_.fridayClockFmt;
        break;
    }
    displayManager.setDefaultState(state);
  }

  Phase currentPhase_ = Phase::kNone;
  ClockConfig settings_;
};

FridayModeController fridayModeController;

}  // namespace

void fridayModeApplySettings(const ClockConfig& config) {
  fridayModeController.applySettings(config);
}

void fridayModeTick(const DateTime& now) {
  fridayModeController.tick(now);
}
