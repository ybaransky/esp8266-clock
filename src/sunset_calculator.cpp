#include "sunset_calculator.h"

#include <SolarCalculator.h>
#include <math.h>

namespace {

DateTime fallbackSunset(const DateTime& localDate) {
  return DateTime(localDate.year(), localDate.month(), localDate.day(), 18, 0, 0);
}

int32_t roundedSecondsFromHours(double hours) {
  return static_cast<int32_t>(hours * 3600.0 + (hours >= 0.0 ? 0.5 : -0.5));
}

int32_t secondsFromUtcOffsetMinutes(int16_t utcOffsetMinutes) {
  return static_cast<int32_t>(utcOffsetMinutes) * 60;
}

DateTime utcDateForLocalSunsetDate(const DateTime& localDate, int16_t utcOffsetMinutes) {
  const DateTime localEvening(localDate.year(), localDate.month(), localDate.day(), 18, 0, 0);
  return localEvening - TimeSpan(secondsFromUtcOffsetMinutes(utcOffsetMinutes));
}

}  // namespace

DateTime calculateSunset(const DateTime& localDate, const Location& location) {
  if ((location.latitude < -90.0f) || (location.latitude > 90.0f) ||
      (location.longitude < -180.0f) || (location.longitude > 180.0f)) {
    return fallbackSunset(localDate);
  }

  double transit = 0.0;
  double sunrise = 0.0;
  double sunset = 0.0;
  const DateTime utcDate = utcDateForLocalSunsetDate(localDate, location.utcOffsetMinutes);
  calcSunriseSunset(utcDate.year(),
                    utcDate.month(),
                    utcDate.day(),
                    location.latitude,
                    location.longitude,
                    transit,
                    sunrise,
                    sunset);

  if (isnan(sunset)) {
    return fallbackSunset(localDate);
  }

  const DateTime utcMidnight(utcDate.year(), utcDate.month(), utcDate.day(), 0, 0, 0);
  const DateTime utcSunset = utcMidnight + TimeSpan(roundedSecondsFromHours(sunset));
  return utcSunset + TimeSpan(secondsFromUtcOffsetMinutes(location.utcOffsetMinutes));
}
