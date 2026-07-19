#include "friday_mode.h"

#include "display_manager.h"
#include "log.h"
#include "sunset_calculator.h"

namespace {

constexpr int32_t kSunsetMessageMs = 5000;

// Couples the Friday schedule phase with the base view that should represent it.
struct FridayScheduleResult {
  FridayPhase phase = FridayPhase::kClock;  // Phase selected for the current time.
  ViewState view;  // Base display view for the selected phase.
};

FridayScheduleResult evaluateFridaySchedule(
    const DateTime& now, const DateTime& fridaySunset,
    const DateTime& saturdaySunset, const FridayConfig& formats) {
  FridayScheduleResult result;
  result.phase = evaluateFridayPhase(now.unixtime(), fridaySunset.unixtime(),
                                     saturdaySunset.unixtime());
  switch (result.phase) {
    case FridayPhase::kToFridaySunset:
      result.view = {View::kCountdown, fridaySunset,
                     formats.toFridaySunsetFmt};
      break;
    case FridayPhase::kToSaturdaySunset:
      result.view = {View::kCountdown, saturdaySunset,
                     formats.toSaturdaySunsetFmt};
      break;
    case FridayPhase::kClock:
    case FridayPhase::kNone:
      result.view.view = View::kClock;
      result.view.formatIndex = formats.clockFmt;
      break;
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

}  // namespace

void FridayModeController::applySettings(const ClockConfig& config) {
  settings_.activeMode = config.activeMode;
  settings_.formats = config.friday;
  settings_.location = {config.locations.device.latitude,
                        config.locations.device.longitude,
                        config.timezone.utcOffsetMinutes};
  strlcpy(settings_.sunsetMessage, config.messages.fridaySunset,
          sizeof(settings_.sunsetMessage));
  currentPhase_ = FridayPhase::kNone;
  cachedFridayDate_ = DateTime();
}

void FridayModeController::resetSunsetCache() {
  cachedFridayDate_ = DateTime();
}

void FridayModeController::tick(const DateTime& now,
                                DisplayManager& displayManager) {
  if (settings_.activeMode != kModeFriday) return;

  refreshSunsetCacheIfNeeded(now);
  const FridayScheduleResult result = evaluateFridaySchedule(
      now, cachedFridaySunset_, cachedSaturdaySunset_, settings_.formats);
  if (result.phase == currentPhase_) return;

  LOG_PRINTF("friday mode: phase -> %s", fridayPhaseName(result.phase));
  const bool crossedFridaySunset =
      (currentPhase_ == FridayPhase::kToFridaySunset) &&
      (result.phase == FridayPhase::kToSaturdaySunset);
  currentPhase_ = result.phase;
  displayManager.setView(result.view);

  if (crossedFridaySunset) {
    displayManager.showInfo(settings_.sunsetMessage, kSunsetMessageMs);
  }
}

DateTime FridayModeController::fridayDateFor(const DateTime& now) {
  const uint8_t dow = now.dayOfTheWeek();
  const uint32_t todayMidnight =
      DateTime(now.year(), now.month(), now.day(), 0, 0, 0).unixtime();
  return DateTime(mostRecentFridayMidnight(todayMidnight, dow));
}

void FridayModeController::refreshSunsetCacheIfNeeded(const DateTime& now) {
  const DateTime fridayDate = fridayDateFor(now);
  if (fridayDate.unixtime() == cachedFridayDate_.unixtime()) return;

  cachedFridayDate_ = fridayDate;
  const DateTime saturdayDate(fridayDate.unixtime() + 86400UL);
  cachedFridaySunset_ = calculateSunset(fridayDate, settings_.location);
  cachedSaturdaySunset_ = calculateSunset(saturdayDate, settings_.location);
  LOG_PRINTF("friday mode: recomputed sunsets for %04d-%02d-%02d",
             fridayDate.year(), fridayDate.month(), fridayDate.day());
}
