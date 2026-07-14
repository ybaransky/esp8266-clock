#include "friday_mode.h"

#include "display_manager.h"
#include "log.h"
#include "sunset_calculator.h"

namespace {

constexpr int32_t kSunsetMessageMs = 5000;

enum class FridayPhase : uint8_t {
  kNone, kClock, kToFridaySunset, kToSaturdaySunset,
};

// Couples the Friday schedule phase with the base view that should represent it.
struct FridayScheduleResult {
  FridayPhase phase = FridayPhase::kClock;  // Phase selected for the current time.
  ViewState view;  // Base display view for the selected phase.
};

FridayScheduleResult evaluateFridaySchedule(
    const DateTime& now, const DateTime& fridaySunset,
    const DateTime& saturdaySunset, const ClockConfig& config) {
  FridayScheduleResult result;
  const uint32_t nowUnix = now.unixtime();
  if (nowUnix < fridaySunset.unixtime()) {
    result.phase = FridayPhase::kToFridaySunset;
    result.view = {View::kCountdown, fridaySunset,
                   config.friday.toFridaySunsetFmt};
  } else if (nowUnix < saturdaySunset.unixtime()) {
    result.phase = FridayPhase::kToSaturdaySunset;
    result.view = {View::kCountdown, saturdaySunset,
                   config.friday.toSaturdaySunsetFmt};
  } else {
    result.view.view = View::kClock;
    result.view.formatIndex = config.friday.clockFmt;
  }
  return result;
}

const char* fridayPhaseName(FridayPhase phase) {
  switch (phase) {
    case FridayPhase::kToFridaySunset: return "to friday sunset";
    case FridayPhase::kToSaturdaySunset: return "to saturday sunset";
    default: return "clock";
  }
}

// Computes Friday phases, caches weekly sunsets, and pushes view transitions to the display.
class FridayModeController {
 public:
  void applySettings(const ClockConfig& config) {
    settings_ = config;
    currentPhase_ = FridayPhase::kNone;
    cachedFridayDate_ = DateTime();  // invalidate cache
  }

  void resetSunsetCache() {
    cachedFridayDate_ = DateTime();
  }

  void tick(const DateTime& now, DisplayManager& displayManager) {
    if (settings_.activeMode != kModeFriday) return;

    refreshSunsetCacheIfNeeded(now);
    const FridayScheduleResult result = evaluateFridaySchedule(
        now, cachedFridaySunset_, cachedSaturdaySunset_, settings_);

    if (result.phase == currentPhase_) return;
    LOG_PRINTF("friday mode: phase -> %s\n", fridayPhaseName(result.phase));

    // Only a live kToFridaySunset -> kToSaturdaySunset crossing announces
    // sunset. Arriving at kToSaturdaySunset from kNone (boot or a config
    // save on a Friday evening) is not the sun going down.
    const bool crossedFridaySunset =
        currentPhase_ == FridayPhase::kToFridaySunset &&
        result.phase == FridayPhase::kToSaturdaySunset;

    currentPhase_ = result.phase;
    displayManager.setView(result.view);

    if (crossedFridaySunset) {
      // The Saturday-sunset countdown is already the base view, so it is
      // revealed when this overlay expires.
      displayManager.showInfo(settings_.messages.fridaySunset, kSunsetMessageMs);
    }
  }

 private:
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
    const Location loc{settings_.locations.device.latitude,
                       settings_.locations.device.longitude,
                       settings_.timezone.utcOffsetMinutes};
    cachedFridaySunset_   = calculateSunset(fridayDate,   loc);
    cachedSaturdaySunset_ = calculateSunset(saturdayDate, loc);
    LOG_PRINTF("friday mode: recomputed sunsets for %04d-%02d-%02d\n",
               fridayDate.year(), fridayDate.month(), fridayDate.day());
  }

  FridayPhase currentPhase_ = FridayPhase::kNone;  // Last phase applied to the display.
  ClockConfig settings_;  // Configuration snapshot used for schedule evaluation.
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

void fridayModeTick(const DateTime& now, DisplayManager& displayManager) {
  fridayModeController.tick(now, displayManager);
}
