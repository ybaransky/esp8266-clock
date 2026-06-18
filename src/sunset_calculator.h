#pragma once

#include <RTClib.h>

struct Location {
  float latitude;
  float longitude;
};

// Stub for Friday mode. The real astronomical implementation belongs behind
// this function so Friday mode can stay focused on phase and target selection.
DateTime calculateSunset(const DateTime& localDate, const Location& location);
