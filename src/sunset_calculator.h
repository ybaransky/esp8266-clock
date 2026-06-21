#pragma once

#include <RTClib.h>

struct Location {
  float latitude;
  float longitude;
  int16_t utcOffsetMinutes = 0;
};

// Returns local sunset time for the given local date and location.
DateTime calculateSunset(const DateTime& localDate, const Location& location);
