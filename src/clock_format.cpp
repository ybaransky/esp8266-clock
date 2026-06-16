#include "clock_format.h"
#include <stdio.h>

namespace {
  // Right-justify a 2-digit elapsed value.
  // Produces "  " (two spaces) when val == 0 — hides leading zero units
  // (e.g. "0 days" or "0 hours" should not clutter the display).
  void bp(char* out, int val) {
    if (val == 0) { out[0] = ' '; out[1] = ' '; out[2] = '\0'; }
    else          snprintf(out, 4, "%2d", val);
  }
}

// ── Countdown ─────────────────────────────────────────────────────────────────
// Padding rules applied below:
//   dd, hh  → bp() : right-justified, blank when zero
//   mm, ss  → %02d after ':', else %2d
//   u       → single digit

void renderCountdown(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3) {
  char dd[4], hh[4];
  bp(dd, f.days);
  bp(hh, f.hours);

  switch (idx) {
    default:
    case 0: // dd D | hh:mm | ss.u
      snprintf(r1, 8, "%s D", dd);
      snprintf(r2, 8, "%s:%02d", hh, f.minutes);
      snprintf(r3, 8, "%2d.%d", f.seconds, f.tenths);
      break;
    case 1: // dd D | hh:mm | ss
      snprintf(r1, 8, "%s D", dd);
      snprintf(r2, 8, "%s:%02d", hh, f.minutes);
      snprintf(r3, 8, "%2d", f.seconds);
      break;
    case 2: // dd D | hh H | mm:ss
      snprintf(r1, 8, "%s D", dd);
      snprintf(r2, 8, "%s H", hh);
      snprintf(r3, 8, "%02d:%02d", f.minutes, f.seconds);
      break;
    case 3: // dd D | hh H | mm N
      snprintf(r1, 8, "%s D", dd);
      snprintf(r2, 8, "%s H", hh);
      snprintf(r3, 8, "%2d N", f.minutes);
      break;
    case 4: // dd | hh:mm | ss.u
      snprintf(r1, 8, "%s", dd);
      snprintf(r2, 8, "%s:%02d", hh, f.minutes);
      snprintf(r3, 8, "%2d.%d", f.seconds, f.tenths);
      break;
    case 5: // dd | hh:mm | ss
      snprintf(r1, 8, "%s", dd);
      snprintf(r2, 8, "%s:%02d", hh, f.minutes);
      snprintf(r3, 8, "%2d", f.seconds);
      break;
    case 6: // dd | hh | mm:ss
      snprintf(r1, 8, "%s", dd);
      snprintf(r2, 8, "%s", hh);
      snprintf(r3, 8, "%02d:%02d", f.minutes, f.seconds);
      break;
    case 7: // dd | hh | mm
      snprintf(r1, 8, "%s", dd);
      snprintf(r2, 8, "%s", hh);
      snprintf(r3, 8, "%2d", f.minutes);
      break;
  }
}

void renderCountup(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3) {
  renderCountdown(idx, f, r1, r2, r3);
}

// ── Clock ─────────────────────────────────────────────────────────────────────
// All calendar fields (YYYY, MM, DD) are zero-padded — they are real dates,
// not elapsed values, so "0 hours" does not apply.
// hh uses %02d here for the same reason (a clock at midnight shows "00", not "  ").

void renderClock(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3, bool colonVisible) {
  const char c = colonVisible ? ':' : ' ';

  switch (idx) {
    default:
    case 0: // YYYY | MM:DD | hh:mm  (blink colon on hh:mm)
      snprintf(r1, 8, "%4d", f.year);
      snprintf(r2, 8, "%02d:%02d", f.month, f.dayOfMonth);
      snprintf(r3, 8, "%02d%c%02d", f.hours, c, f.minutes);
      break;
    case 1: // YYYY | MM:DD | hh:mm  (static colon)
      snprintf(r1, 8, "%4d", f.year);
      snprintf(r2, 8, "%02d:%02d", f.month, f.dayOfMonth);
      snprintf(r3, 8, "%02d:%02d", f.hours, f.minutes);
      break;
    case 2: // YYYY | MM | DD
      snprintf(r1, 8, "%4d", f.year);
      snprintf(r2, 8, "%2d", f.month);
      snprintf(r3, 8, "%2d", f.dayOfMonth);
      break;
    case 3: // MM | DD | hh:mm  (blink colon on hh:mm)
      snprintf(r1, 8, "%2d", f.month);
      snprintf(r2, 8, "%2d", f.dayOfMonth);
      snprintf(r3, 8, "%02d%c%02d", f.hours, c, f.minutes);
      break;
    case 4: // MM | DD | hh:mm  (static colon)
      snprintf(r1, 8, "%2d", f.month);
      snprintf(r2, 8, "%2d", f.dayOfMonth);
      snprintf(r3, 8, "%02d:%02d", f.hours, f.minutes);
      break;
    case 5: // MM:DD | hh:mm | ss u
      snprintf(r1, 8, "%02d:%02d", f.month, f.dayOfMonth);
      snprintf(r2, 8, "%02d:%02d", f.hours, f.minutes);
      snprintf(r3, 8, "%02d %d", f.seconds, f.tenths);
      break;
    case 6: // MM:DD | hh:mm | ss
      snprintf(r1, 8, "%02d:%02d", f.month, f.dayOfMonth);
      snprintf(r2, 8, "%02d:%02d", f.hours, f.minutes);
      snprintf(r3, 8, "%2d", f.seconds);
      break;
    case 7: // MM:DD | hh | mm:ss
      snprintf(r1, 8, "%02d:%02d", f.month, f.dayOfMonth);
      snprintf(r2, 8, "%2d", f.hours);
      snprintf(r3, 8, "%02d:%02d", f.minutes, f.seconds);
      break;
    case 8: // MM:DD | hh | mm
      snprintf(r1, 8, "%02d:%02d", f.month, f.dayOfMonth);
      snprintf(r2, 8, "%2d", f.hours);
      snprintf(r3, 8, "%2d", f.minutes);
      break;
    case 9: // DD | hh:mm | ss u
      snprintf(r1, 8, "%2d", f.dayOfMonth);
      snprintf(r2, 8, "%02d:%02d", f.hours, f.minutes);
      snprintf(r3, 8, "%02d %d", f.seconds, f.tenths);
      break;
    case 10: // DD | hh:mm | ss
      snprintf(r1, 8, "%2d", f.dayOfMonth);
      snprintf(r2, 8, "%02d:%02d", f.hours, f.minutes);
      snprintf(r3, 8, "%2d", f.seconds);
      break;
    case 11: // DD | hh | mm:ss
      snprintf(r1, 8, "%2d", f.dayOfMonth);
      snprintf(r2, 8, "%2d", f.hours);
      snprintf(r3, 8, "%02d:%02d", f.minutes, f.seconds);
      break;
    case 12: // DD | hh | mm
      snprintf(r1, 8, "%2d", f.dayOfMonth);
      snprintf(r2, 8, "%2d", f.hours);
      snprintf(r3, 8, "%2d", f.minutes);
      break;
  }
}
