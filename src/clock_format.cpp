#include "clock_format.h"
#include <stdio.h>

namespace {
  // Right-justify a 2-digit elapsed value.
  // Produces "  " (two spaces) when val == 0 - hides leading zero units
  // (e.g. "0 days" or "0 hours" should not clutter the display).
  void bp(char* out, int val) {
    if (val == 0) { out[0] = ' '; out[1] = ' '; out[2] = '\0'; }
    else          snprintf(out, 4, "%2d", val);
  }

  void hp(char* out, int val) {
    snprintf(out, 4, "%2d", val);
  }

  void formatDaysWithLabel(char* out, int days) {
    const int displayDays = days < 0 ? 0 : (days > 9999 ? 9999 : days);
    if (displayDays < 10) {
      snprintf(out, 8, " %d d", displayDays);
      return;
    }
    if (displayDays < 100) {
      snprintf(out, 8, "%2d d", displayDays);
      return;
    }
    if (displayDays < 1000) {
      snprintf(out, 8, "%3dd", displayDays);
      return;
    }
    snprintf(out, 8, "%4d", displayDays);
  }

  void formatDaysRightJustified(char* out, int days) {
    const int displayDays = days < 0 ? 0 : (days > 9999 ? 9999 : days);
    snprintf(out, 8, "%4d", displayDays);
  }
}

// -- Countdown -----------------------------------------------------------------
// Padding rules applied below:
//   dd      -> formats 0-3 use compact d suffix; formats 4-7 right-justify
//   hh      -> hp() before ':', bp() otherwise
//   mm, ss  -> %02d after ':', else %2d
//   u       -> single digit

void renderCountdown(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3) {
  char hh[4], hhColon[4];
  bp(hh, f.hours);
  hp(hhColon, f.hours);

  switch (idx) {
    default:
    case 0: // dd d | hh:mm | ss.u
      formatDaysWithLabel(r1, f.days);
      snprintf(r2, 8, "%s:%02d", hhColon, f.minutes);
      snprintf(r3, 8, "%2d.%d", f.seconds, f.tenths);
      break;
    case 1: // dd d | hh:mm | ss
      formatDaysWithLabel(r1, f.days);
      snprintf(r2, 8, "%s:%02d", hhColon, f.minutes);
      snprintf(r3, 8, "%2d", f.seconds);
      break;
    case 2: // dd d | hh H | mm:ss
      formatDaysWithLabel(r1, f.days);
      snprintf(r2, 8, "%s H", hh);
      snprintf(r3, 8, "%02d:%02d", f.minutes, f.seconds);
      break;
    case 3: // dd d | hh H | mm N
      formatDaysWithLabel(r1, f.days);
      snprintf(r2, 8, "%s H", hh);
      snprintf(r3, 8, "%2d N", f.minutes);
      break;
    case 4: // dd | hh:mm | ss.u
      formatDaysRightJustified(r1, f.days);
      snprintf(r2, 8, "%s:%02d", hhColon, f.minutes);
      snprintf(r3, 8, "%2d.%d", f.seconds, f.tenths);
      break;
    case 5: // dd | hh:mm | ss
      formatDaysRightJustified(r1, f.days);
      snprintf(r2, 8, "%s:%02d", hhColon, f.minutes);
      snprintf(r3, 8, "%2d", f.seconds);
      break;
    case 6: // dd | hh | mm:ss
      formatDaysRightJustified(r1, f.days);
      snprintf(r2, 8, "%s", hh);
      snprintf(r3, 8, "%02d:%02d", f.minutes, f.seconds);
      break;
    case 7: // dd | hh | mm
      formatDaysRightJustified(r1, f.days);
      snprintf(r2, 8, "%s", hh);
      snprintf(r3, 8, "%2d", f.minutes);
      break;
  }
}

void renderCountup(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3) {
  renderCountdown(idx, f, r1, r2, r3);
}

// -- Clock ---------------------------------------------------------------------
// All calendar fields (YYYY, MM, DD) are zero-padded - they are real dates,
// not elapsed values, so "0 hours" does not apply.
// hh:mm uses %2d so midnight shows " 0:00", with 0 right-justified to the colon.

void renderClock(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3, bool colonVisible) {
  const char c = colonVisible ? ':' : ' ';

  switch (idx) {
    default:
    case 0: // YYYY | MM:DD | hh:mm  (blink colon on hh:mm)
      snprintf(r1, 8, "%4d", f.year);
      snprintf(r2, 8, "%02d:%02d", f.month, f.dayOfMonth);
      snprintf(r3, 8, "%2d%c%02d", f.hours, c, f.minutes);
      break;
    case 1: // YYYY | MM:DD | hh:mm  (static colon)
      snprintf(r1, 8, "%4d", f.year);
      snprintf(r2, 8, "%02d:%02d", f.month, f.dayOfMonth);
      snprintf(r3, 8, "%2d:%02d", f.hours, f.minutes);
      break;
    case 2: // YYYY | MM | DD
      snprintf(r1, 8, "%4d", f.year);
      snprintf(r2, 8, "%2d", f.month);
      snprintf(r3, 8, "%2d", f.dayOfMonth);
      break;
    case 3: // MM | DD | hh:mm  (blink colon on hh:mm)
      snprintf(r1, 8, "%2d", f.month);
      snprintf(r2, 8, "%2d", f.dayOfMonth);
      snprintf(r3, 8, "%2d%c%02d", f.hours, c, f.minutes);
      break;
    case 4: // MM | DD | hh:mm  (static colon)
      snprintf(r1, 8, "%2d", f.month);
      snprintf(r2, 8, "%2d", f.dayOfMonth);
      snprintf(r3, 8, "%2d:%02d", f.hours, f.minutes);
      break;
    case 5: // MM:DD | hh:mm | ss u
      snprintf(r1, 8, "%02d:%02d", f.month, f.dayOfMonth);
      snprintf(r2, 8, "%2d:%02d", f.hours, f.minutes);
      snprintf(r3, 8, "%02d %d", f.seconds, f.tenths);
      break;
    case 6: // MM:DD | hh:mm | ss
      snprintf(r1, 8, "%02d:%02d", f.month, f.dayOfMonth);
      snprintf(r2, 8, "%2d:%02d", f.hours, f.minutes);
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
      snprintf(r2, 8, "%2d:%02d", f.hours, f.minutes);
      snprintf(r3, 8, "%02d %d", f.seconds, f.tenths);
      break;
    case 10: // DD | hh:mm | ss
      snprintf(r1, 8, "%2d", f.dayOfMonth);
      snprintf(r2, 8, "%2d:%02d", f.hours, f.minutes);
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
