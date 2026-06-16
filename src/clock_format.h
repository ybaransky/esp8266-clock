#pragma once
#include <stdint.h>

// ── Time value bundle ─────────────────────────────────────────────────────────
struct TimeFields {
  int year, month, dayOfMonth;  // calendar values (Clock mode)
  int days;                      // elapsed/remaining days (CountUp/Down)
  int hours, minutes, seconds, tenths;
};

// ── Update-rate / feature queries ─────────────────────────────────────────────
// True when the format shows tenths → caller should refresh at 100 ms.
inline bool countdownHasTenths (uint8_t idx) { return idx == 0 || idx == 4; }
inline bool countupHasTenths   (uint8_t idx) { return idx == 0 || idx == 4; }
inline bool clockHasTenths     (uint8_t idx) { return idx == 5 || idx == 9; }
// True for clock formats whose time-separator colon blinks (caller controls colonVisible).
inline bool clockBlinkColon(uint8_t idx) { return idx == 0 || idx == 3; }

// ── Renderers ─────────────────────────────────────────────────────────────────
// Each fills r1/r2/r3 (null-terminated, must be >= 8 bytes each).
// Countdown and CountUp share the same format table.
void renderCountdown(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3);
void renderCountup  (uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3);
// colonVisible only matters when clockBlinkColon(idx) is true.
void renderClock    (uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3,
                     bool colonVisible = true);
