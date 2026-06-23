#include "clock_format.h"
#include <stdio.h>

namespace {

// Right-justify a 2-digit elapsed value.
// Produces "  " (two spaces) when val == 0 — suppresses leading zero units
// (e.g. "0 hours" on countdown should not clutter the display).
void fmtBlankPadded(char* out, int val) {
  if (val == 0) { out[0] = ' '; out[1] = ' '; out[2] = '\0'; }
  else          snprintf(out, 4, "%2d", val);
}

void fmtZeroPadded(char* out, int val) {
  snprintf(out, 4, "%2d", val);
}

void fmtNumber(char* out, int val) {
  snprintf(out, 8, "%4d", val);
}

void fmtText(char* out, const char* text) {
  snprintf(out, 8, "%4s", text);
}

void fmtDaysWithLabel(char* out, int days) {
  const int d = days < 0 ? 0 : (days > 9999 ? 9999 : days);
  if (d < 10)   { snprintf(out, 8, " %d d", d);  return; }
  if (d < 100)  { snprintf(out, 8, "%2d d", d);  return; }
  if (d < 1000) { snprintf(out, 8, "%3dd",  d);  return; }
  snprintf(out, 8, "%4d", d);
}

void fmtDaysRight(char* out, int days) {
  const int d = days < 0 ? 0 : (days > 9999 ? 9999 : days);
  snprintf(out, 8, "%4d", d);
}

const char* dowAbbrev(int dayOfWeek) {
  //static const char* const kNames[] = {"Sun", "mon", "tu", "wEd", "thu", "Fri", "Sat"};
  static const char* const kNames[] = {"Sun", "non", "tu", "uEd", "thu", "Fri", "Sat"};
  if (dayOfWeek < 0 || dayOfWeek >= static_cast<int>(sizeof(kNames) / sizeof(kNames[0]))) {
    return "   ";
  }
  return kNames[dayOfWeek];
}

void fmtMonthDay(char* out, int month, int day) {
  snprintf(out, 8, "%2d:%02d", month, day);
}

void fmtHourMin(char* out, int hours, int minutes) {
  snprintf(out, 8, "%2d:%02d", hours, minutes);
}

void fmtHourMinBlink(char* out, int hours, int minutes, bool colonVisible) {
  if (colonVisible) snprintf(out, 8, "%2d:%02d", hours, minutes);
  else              snprintf(out, 8, "%2d%02d",  hours, minutes);
}

void fmtMinSec(char* out, int minutes, int seconds) {
  snprintf(out, 8, "%2d:%02d", minutes, seconds);
}

}  // namespace

// -- Countdown -----------------------------------------------------------------
// Padding rules applied below:
//   dd      -> formats 0-3 use compact d suffix; formats 4-7 right-justify
//   hh      -> fmtZeroPadded before ':', fmtBlankPadded otherwise
//   mm, ss  -> %02d after ':', else %2d
//   u       -> single digit

void renderCountdown(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3) {
  char hh[4], hhColon[4];
  fmtBlankPadded(hh, f.hours);
  fmtZeroPadded(hhColon, f.hours);

  switch (idx) {
    default:
    case 0: // dd d | hh:mm | ss.u
      fmtDaysWithLabel(r1, f.days);
      snprintf(r2, 8, "%s:%02d", hhColon, f.minutes);
      snprintf(r3, 8, "%2d.%d", f.seconds, f.tenths);
      break;
    case 1: // dd d | hh:mm | ss
      fmtDaysWithLabel(r1, f.days);
      snprintf(r2, 8, "%s:%02d", hhColon, f.minutes);
      fmtNumber(r3, f.seconds);
      break;
    case 2: // dd d | hh H | mm:ss
      fmtDaysWithLabel(r1, f.days);
      snprintf(r2, 8, "%s H", hh);
      fmtMinSec(r3, f.minutes, f.seconds);
      break;
    case 3: // dd d | hh H | mm N
      fmtDaysWithLabel(r1, f.days);
      snprintf(r2, 8, "%s H", hh);
      snprintf(r3, 8, "%2d N", f.minutes);
      break;
    case 4: // dd | hh:mm | ss.u
      fmtDaysRight(r1, f.days);
      snprintf(r2, 8, "%s:%02d", hhColon, f.minutes);
      snprintf(r3, 8, "%2d.%d", f.seconds, f.tenths);
      break;
    case 5: // dd | hh:mm | ss
      fmtDaysRight(r1, f.days);
      snprintf(r2, 8, "%s:%02d", hhColon, f.minutes);
      fmtNumber(r3, f.seconds);
      break;
    case 6: // dd | hh | mm:ss
      fmtDaysRight(r1, f.days);
      fmtText(r2, hh);
      fmtMinSec(r3, f.minutes, f.seconds);
      break;
    case 7: // dd | hh | mm
      fmtDaysRight(r1, f.days);
      fmtText(r2, hh);
      fmtNumber(r3, f.minutes);
      break;
  }
}

void renderCountup(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3) {
  renderCountdown(idx, f, r1, r2, r3);
}

// -- Clock ---------------------------------------------------------------------
// All calendar fields (YYYY, MM, DD) are zero-padded - they are real dates,
// not elapsed values, so "0 hours" suppression does not apply.
// hh:mm uses %2d so midnight shows " 0:00", right-justified to the colon.

void renderClock(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3, bool colonVisible) {
  const char* dow = dowAbbrev(f.dayOfWeek);

  switch (idx) {
    default:
    case 0: // DOFW | MM:DD | hh:mm  (blink colon)
      fmtText(r1, dow);
      fmtMonthDay(r2, f.month, f.dayOfMonth);
      fmtHourMinBlink(r3, f.hours, f.minutes, colonVisible);
      break;
    case 1: // DOFW |       | hh;mm  (blink colon)
      fmtText(r1, dow);
      fmtText(r2, "    ");
      fmtHourMinBlink(r3, f.hours, f.minutes, colonVisible);
      break;
    case 2: // DOFW | MM | DD
      fmtText(r1, dow);
      fmtNumber(r2, f.month);
      fmtNumber(r3, f.dayOfMonth);
      break;
    case 3: // DOFW | hh:mm | ss:u
      fmtText(r1, dow);
      fmtHourMin(r2, f.hours, f.minutes);
      snprintf(r3, 8, "%2d:%d", f.seconds, f.tenths);
      break;
    case 4: // DOFW | hh:mm | ss
      fmtText(r1, dow);
      fmtHourMin(r2, f.hours, f.minutes);
      fmtNumber(r3, f.seconds);
      break;
    case 5: // DOFW | hh h | mm:ss
      fmtText(r1, dow);
      snprintf(r2, 8, "%2d h", f.hours);
      fmtMinSec(r3, f.minutes, f.seconds);
      break;
    case 6: // DOFW | hh h | mm n
      fmtText(r1, dow);
      snprintf(r2, 8, "%2d h", f.hours);
      snprintf(r3, 8, "%2d n", f.minutes);
      break;
    case 7: // YYYY | MM:DD | hh;mm  (blink colon)
      snprintf(r1, 8, "%4d", f.year);
      fmtMonthDay(r2, f.month, f.dayOfMonth);
      fmtHourMinBlink(r3, f.hours, f.minutes, colonVisible);
      break;
    case 8: // YYYY | MM | DD
      snprintf(r1, 8, "%4d", f.year);
      fmtNumber(r2, f.month);
      fmtNumber(r3, f.dayOfMonth);
      break;
    case 9: // MM | DD | hh;mm  (blink colon)
      fmtNumber(r1, f.month);
      fmtNumber(r2, f.dayOfMonth);
      fmtHourMinBlink(r3, f.hours, f.minutes, colonVisible);
      break;
    case 10: // MM:DD | hh:mm | ss u
      fmtMonthDay(r1, f.month, f.dayOfMonth);
      fmtHourMin(r2, f.hours, f.minutes);
      snprintf(r3, 8, "%02d %d", f.seconds, f.tenths);
      break;
    case 11: // MM:DD | hh:mm | ss
      fmtMonthDay(r1, f.month, f.dayOfMonth);
      fmtHourMin(r2, f.hours, f.minutes);
      fmtNumber(r3, f.seconds);
      break;
    case 12: // MM:DD | hh | mm:ss
      fmtMonthDay(r1, f.month, f.dayOfMonth);
      fmtNumber(r2, f.hours);
      fmtMinSec(r3, f.minutes, f.seconds);
      break;
    case 13: // MM:DD | hh | mm
      fmtMonthDay(r1, f.month, f.dayOfMonth);
      fmtNumber(r2, f.hours);
      fmtNumber(r3, f.minutes);
      break;
    case 14: // DD | hh:mm | ss u
      fmtNumber(r1, f.dayOfMonth);
      fmtHourMin(r2, f.hours, f.minutes);
      snprintf(r3, 8, "%02d %d", f.seconds, f.tenths);
      break;
    case 15: // DD | hh:mm | ss
      fmtNumber(r1, f.dayOfMonth);
      fmtHourMin(r2, f.hours, f.minutes);
      fmtNumber(r3, f.seconds);
      break;
    case 16: // DD | hh | mm:ss
      fmtNumber(r1, f.dayOfMonth);
      fmtNumber(r2, f.hours);
      fmtMinSec(r3, f.minutes, f.seconds);
      break;
    case 17: // DD | hh | mm
      fmtNumber(r1, f.dayOfMonth);
      fmtNumber(r2, f.hours);
      fmtNumber(r3, f.minutes);
      break;
  }
}
