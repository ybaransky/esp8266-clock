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

// -- Update-rate / feature queries ---------------------------------------------
// Driven by FormatMetadata tables in format.cpp — no hardcoded index lists here.
inline bool countdownHasTenths(uint8_t idx) {
  const FormatMetadata* m = getFormatMeta(kFmtGroupCountdown, idx);
  return m != nullptr && m->hasTenths;
}
inline bool countupHasTenths(uint8_t idx) {
  const FormatMetadata* m = getFormatMeta(kFmtGroupCountUp, idx);
  return m != nullptr && m->hasTenths;
}
inline bool clockHasTenths(uint8_t idx) {
  const FormatMetadata* m = getFormatMeta(kFmtGroupClock, idx);
  return m != nullptr && m->hasTenths;
}
// True for clock formats whose time-separator colon blinks (caller controls colonVisible).
inline bool clockBlinkColon(uint8_t idx) {
  const FormatMetadata* m = getFormatMeta(kFmtGroupClock, idx);
  return m != nullptr && m->blinkColon;
}

// -- Renderers -----------------------------------------------------------------
// Each fills r1/r2/r3 (null-terminated, must be >= 8 bytes each).
// Countdown and CountUp share the same format table.
void renderCountdown(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3);
void renderCountup  (uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3);
// colonVisible only matters when clockBlinkColon(idx) is true.
void renderClock    (uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3,
                     bool colonVisible = true);

// Validates static renderer-table coverage against format table counts.
bool clockFormatValidateInvariants();
