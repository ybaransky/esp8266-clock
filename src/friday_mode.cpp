#include "friday_mode.h"

#include "config_validation.h"
#include "display_manager.h"
#include "log.h"
#include "sound_player.h"
#include "sunset_calculator.h"

namespace {

constexpr int32_t kSunsetMessageMs = 5000;

// Couples the Friday schedule phase with the base view that should represent it.
struct FridayScheduleResult {
  FridayPhase phase = FridayPhase::kClock;  // Phase selected for the current time.
  ViewState view;  // Base display view for the selected phase.
};

// Builds the blink window that ends at Friday sunset, or an empty (disabled)
// one when the configured length is zero.
BlinkWindow blinkWindowBefore(const DateTime& fridaySunset, uint8_t minutes) {
  if (minutes == 0) return {};
  const uint32_t sunsetUnix = fridaySunset.unixtime();
  return {sunsetUnix - minutes * 60UL, sunsetUnix};
}

// Builds the blink window that starts at Friday sunset, or an empty (disabled)
// one when the configured length is zero.
BlinkWindow blinkWindowAfter(const DateTime& fridaySunset, uint8_t minutes) {
  if (minutes == 0) return {};
  const uint32_t sunsetUnix = fridaySunset.unixtime();
  return {sunsetUnix, sunsetUnix + minutes * 60UL};
}

FridayScheduleResult evaluateFridaySchedule(
    const DateTime& now, const DateTime& fridaySunset,
    const DateTime& saturdaySunset, const FridayConfig& formats) {
  FridayScheduleResult result;
  result.phase = evaluateFridayPhase(now.unixtime(), fridaySunset.unixtime(),
                                     saturdaySunset.unixtime());
  switch (result.phase) {
    case FridayPhase::kToFridaySunset:
      result.view = {View::kCountdown, fridaySunset,
                     formats.toFridaySunsetFmt, kSameFormat,
                     blinkWindowBefore(fridaySunset,
                                       formats.blinkBeforeMinutes)};
      break;
    case FridayPhase::kToSaturdaySunset:
      // Both windows bracket *Friday* sunset, so the post-sunset window rides
      // on the Saturday-sunset countdown that follows it.
      result.view = {View::kCountdown, saturdaySunset,
                     formats.toSaturdaySunsetFmt, kSameFormat,
                     blinkWindowAfter(fridaySunset,
                                      formats.blinkAfterMinutes)};
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
  strlcpy(settings_.sunsetSound,
          activeSoundName(config.sound, config.sound.fridaySunset),
          sizeof(settings_.sunsetSound));
  settings_.boundaryAlertEnabled =
      config.sound.enabled && config.sound.boundaryAlert.enabled;
  settings_.boundary1 = config.sound.boundaryAlert.boundary1;
  settings_.boundary2 = config.sound.boundaryAlert.boundary2;
  resetSchedule();
}

void FridayModeController::resetSchedule() {
  phaseTracker_.reset();
  cachedFridayDate_ = DateTime();
}

void FridayModeController::tick(const DateTime& now,
                                uint32_t secondStartedAtMs,
                                ModeOutputs& outputs) {
  if (settings_.activeMode != kModeFriday) return;

  refreshSunsetCacheIfNeeded(now);
  const FridayScheduleResult result = evaluateFridaySchedule(
      now, cachedFridaySunset_, cachedSaturdaySunset_, settings_.formats);
  if (!settings_.boundaryAlertEnabled) {
    outputs.sound.cancelBoundaryAlert();
  } else if (result.phase == FridayPhase::kToFridaySunset) {
    outputs.sound.updateBoundaryAlert(cachedFridaySunset_.unixtime(),
        now.unixtime(), secondStartedAtMs, settings_.boundary1.toneHz,
        settings_.boundary1.totalDurationSeconds,
        settings_.boundary1.startingBeatsHz);
  } else if (result.phase == FridayPhase::kToSaturdaySunset) {
    outputs.sound.updateBoundaryAlert(cachedSaturdaySunset_.unixtime(),
        now.unixtime(), secondStartedAtMs, settings_.boundary2.toneHz,
        settings_.boundary2.totalDurationSeconds,
        settings_.boundary2.startingBeatsHz);
  } else {
    outputs.sound.cancelBoundaryAlert();
  }
  const uint32_t targetUnix = result.view.anchor.unixtime();
  if (!phaseTracker_.hasChanged(result.phase, targetUnix)) return;

  const FridayPhase previousPhase = phaseTracker_.phase();
  phaseTracker_.accept(result.phase, targetUnix);

  LOG_PRINTF("friday mode: phase -> %s", fridayPhaseName(result.phase));
  outputs.display.setView(result.view);

  const bool crossedFridaySunset =
      (previousPhase == FridayPhase::kToFridaySunset) &&
      (result.phase == FridayPhase::kToSaturdaySunset);
  if (crossedFridaySunset) {
    outputs.display.showInfo(settings_.sunsetMessage, kSunsetMessageMs);
    // Arriving here from kNone - boot, config reload, a browser time sync -
    // must stay silent, which the previous-phase test above already
    // guarantees for the message and now for the sound too.
    outputs.sound.play(settings_.sunsetSound, millis());
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
