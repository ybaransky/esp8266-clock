#pragma once
#include <stdint.h>
#include "format.h"

// -- Time value bundle ---------------------------------------------------------
struct TimeFields {
  int year, month, dayOfMonth;   // calendar values (Clock mode)
  int dayOfWeek;                 // 0=Sunday through 6=Saturday (Clock mode)
  int days;                      // elapsed/remaining days (CountUp/Down)
  int hours, minutes, seconds, tenths;
};

// -- Renderers -----------------------------------------------------------------
// Each fills r1/r2/r3 (null-terminated, must be >= 8 bytes each).
// Countdown and CountUp share the same format table.
void renderCountdown(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3);
void renderCountup  (uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3);
// colonVisible only matters when clockFormatBlinksColon(idx) is true.
void renderClock    (uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3,
                     bool colonVisible = true);
