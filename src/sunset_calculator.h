#pragma once

#include <RTClib.h>

struct Location {
  float latitude;                  // Observer latitude.
  float longitude;                 // Observer longitude.
  int16_t utcOffsetMinutes = 0;    // Local offset from UTC in minutes.
};

// Returns local sunset time for the given local date and location.
DateTime calculateSunset(const DateTime& localDate, const Location& location);
