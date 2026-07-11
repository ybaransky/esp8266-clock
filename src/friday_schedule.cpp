#include "friday_schedule.h"

FridayScheduleResult evaluateFridaySchedule(
    const DateTime& now,
    const DateTime& fridaySunset,
    const DateTime& saturdaySunset,
    const ClockConfig& config) {
  FridayScheduleResult result;
  const uint32_t nowUnix = now.unixtime();

  if (nowUnix < fridaySunset.unixtime()) {
    result.phase = FridayPhase::kToFridaySunset;
    result.view.view = View::kCountdown;
    result.view.anchor = fridaySunset;
    result.view.formatIndex = config.friday.toFridaySunsetFmt;
    return result;
  }

  if (nowUnix < saturdaySunset.unixtime()) {
    result.phase = FridayPhase::kToSaturdaySunset;
    result.view.view = View::kCountdown;
    result.view.anchor = saturdaySunset;
    result.view.formatIndex = config.friday.toSaturdaySunsetFmt;
    return result;
  }

  result.phase = FridayPhase::kClock;
  result.view.view = View::kClock;
  result.view.formatIndex = config.friday.clockFmt;
  return result;
}

const char* fridayPhaseName(FridayPhase phase) {
  switch (phase) {
    case FridayPhase::kToFridaySunset:
      return "to friday sunset";
    case FridayPhase::kToSaturdaySunset:
      return "to saturday sunset";
    default:
      return "clock";
  }
}
