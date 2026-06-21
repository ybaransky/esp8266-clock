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

}  // namespace

DateTime calculateSunset(const DateTime& localDate, const Location& location) {
  if (location.latitude < -90.0f || location.latitude > 90.0f ||
      location.longitude < -180.0f || location.longitude > 180.0f) {
    return fallbackSunset(localDate);
  }

  double transit = 0.0;
  double sunrise = 0.0;
  double sunset = 0.0;
  calcSunriseSunset(localDate.year(),
                    localDate.month(),
                    localDate.day(),
                    location.latitude,
                    location.longitude,
                    transit,
                    sunrise,
                    sunset);

  if (isnan(sunset)) {
    return fallbackSunset(localDate);
  }

  const double localSunsetHours =
      sunset + static_cast<double>(location.utcOffsetMinutes) / 60.0;
  const DateTime localMidnight(localDate.year(), localDate.month(), localDate.day(), 0, 0, 0);
  return localMidnight + TimeSpan(roundedSecondsFromHours(localSunsetHours));
}
