#include "clock_format.h"
#include <stdio.h>
#include <stdarg.h>

#ifndef CLOCK_FORMAT_DEBUG
#define CLOCK_FORMAT_DEBUG 0
#endif

namespace {

// Device-rendered labels only; format specs remain unchanged.
constexpr char kDayLabel    = 'd';
constexpr char kHourLabel   = 'h';
constexpr char kMinuteLabel = 'n';

#if CLOCK_FORMAT_DEBUG
static uint16_t sClockFormatDebugLines = 0;
static const uint16_t kClockFormatDebugLineLimit = 120;

void clockFormatDebugLog(const char* fmt, ...) {
  if (sClockFormatDebugLines >= kClockFormatDebugLineLimit) return;
  ++sClockFormatDebugLines;

  char buffer[192];
  va_list args;
  va_start(args, fmt);
  vsnprintf(buffer, sizeof(buffer), fmt, args);
  va_end(args);
  Serial.printf("clock_format: %s\n", buffer);
}
#else
void clockFormatDebugLog(const char* /*fmt*/, ...) {}
#endif

void normalizeSpaces(const char* input, char* output, size_t outputSize) {
  if (outputSize == 0) return;
  output[0] = '\0';
  if (input == nullptr) return;

  size_t in = 0;
  while (input[in] == ' ') ++in;

  size_t out = 0;
  bool justWroteSpace = false;
  while (input[in] != '\0' && out + 1 < outputSize) {
    const char c = input[in++];
    if (c == ' ') {
      if (out == 0 || justWroteSpace) continue;
      output[out++] = ' ';
      justWroteSpace = true;
      continue;
    }

    output[out++] = c;
    justWroteSpace = false;
  }

  while (out > 0 && output[out - 1] == ' ') --out;
  output[out] = '\0';
  clockFormatDebugLog("normalize raw='%s' -> norm='%s'", input == nullptr ? "" : input, output);
}

void rightmostSlice(const char* input, size_t width, char* output, size_t outputSize) {
  if (outputSize == 0) return;
  output[0] = '\0';
  if (input == nullptr) return;

  const size_t len = strlen(input);
  const char* start = (len > width) ? input + (len - width) : input;
  snprintf(output, outputSize, "%s", start);
}

// No-colon segments are always right-justified; leading/trailing spaces are
// ignored and multiple internal spaces are collapsed.
void fmtRightSegment(char* out, const char* raw) {
  char normalized[16];
  char sliced[8];
  normalizeSpaces(raw, normalized, sizeof(normalized));
  rightmostSlice(normalized, 4, sliced, sizeof(sliced));
  snprintf(out, 8, "%4s", sliced);
  clockFormatDebugLog("right raw='%s' norm='%s' slice='%s' out='%s'",
                      raw == nullptr ? "" : raw, normalized, sliced, out);
}

// For colon formats, left side is right-justified to the colon and right side
// is left-justified from the colon. Whitespace normalization matches fmtRightSegment().
void fmtColonAnchored(char* out,
                      const char* leftRaw,
                      const char* rightRaw,
                      uint8_t rightWidth,
                      bool colonVisible) {
  char leftNorm[16], rightNorm[16];
  char left2[8], rightN[8];
  normalizeSpaces(leftRaw, leftNorm, sizeof(leftNorm));
  normalizeSpaces(rightRaw, rightNorm, sizeof(rightNorm));
  rightmostSlice(leftNorm, 2, left2, sizeof(left2));
  rightmostSlice(rightNorm, rightWidth, rightN, sizeof(rightN));

  char composed[8];
  size_t pos = 0;

  const size_t leftLen = strlen(left2);
  if (leftLen >= 2) {
    composed[pos++] = left2[leftLen - 2];
    composed[pos++] = left2[leftLen - 1];
  } else if (leftLen == 1) {
    composed[pos++] = ' ';
    composed[pos++] = left2[0];
  } else {
    composed[pos++] = ' ';
    composed[pos++] = ' ';
  }

  if (colonVisible) composed[pos++] = ':';

  const size_t rightLen = strlen(rightN);
  for (uint8_t i = 0; i < rightWidth; ++i) {
    composed[pos++] = (i < rightLen) ? rightN[i] : ' ';
  }

  composed[pos] = '\0';
  snprintf(out, 8, "%s", composed);
  clockFormatDebugLog("colon leftRaw='%s' rightRaw='%s' left='%s' right='%s' out='%s'",
                      leftRaw == nullptr ? "" : leftRaw,
                      rightRaw == nullptr ? "" : rightRaw,
                      left2,
                      rightN,
                      out);
}

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
  fmtRightSegment(out, text);
}

void fmtValueWithLabel(char* out, const char* valueText, char label) {
  char raw[12];
  snprintf(raw, sizeof(raw), "%s %c", valueText == nullptr ? "" : valueText, label);
  fmtRightSegment(out, raw);
}

void fmtIntWithLabel(char* out, int value, char label) {
  char valueText[8];
  snprintf(valueText, sizeof(valueText), "%d", value);
  fmtValueWithLabel(out, valueText, label);
}

void fmtDaysWithLabel(char* out, int days) {
  const int d = days < 0 ? 0 : (days > 9999 ? 9999 : days);
  if (d < 10)   { snprintf(out, 8, " %d %c", d, kDayLabel);  return; }
  if (d < 100)  { snprintf(out, 8, "%2d %c", d, kDayLabel);  return; }
  if (d < 1000) { snprintf(out, 8, "%3d%c",  d, kDayLabel);  return; }
  snprintf(out, 8, "%4d", d);
}

void fmtDaysRight(char* out, int days) {
  const int d = days < 0 ? 0 : (days > 9999 ? 9999 : days);
  snprintf(out, 8, "%4d", d);
}

const char* dowAbbrev(int dayOfWeek) {
  static const char* const kNames[] = {"Sun", "non", "tu", "uEd", "thu", "Fri", "Sat"};
  if (dayOfWeek < 0 || dayOfWeek >= static_cast<int>(sizeof(kNames) / sizeof(kNames[0]))) {
    return "   ";
  }
  return kNames[dayOfWeek];
}

void fmtMonthDay(char* out, int month, int day) {
  char left[8], right[8];
  snprintf(left, sizeof(left), "%d", month);
  snprintf(right, sizeof(right), "%02d", day);
  fmtColonAnchored(out, left, right, 2, true);
}

void fmtHourMin(char* out, int hours, int minutes) {
  char left[8], right[8];
  snprintf(left, sizeof(left), "%d", hours);
  snprintf(right, sizeof(right), "%02d", minutes);
  fmtColonAnchored(out, left, right, 2, true);
}

void fmtHourMinBlink(char* out, int hours, int minutes, bool colonVisible) {
  char left[8], right[8];
  snprintf(left, sizeof(left), "%d", hours);
  snprintf(right, sizeof(right), "%02d", minutes);
  fmtColonAnchored(out, left, right, 2, colonVisible);
}

void fmtMinSec(char* out, int minutes, int seconds) {
  char left[8], right[8];
  snprintf(left, sizeof(left), "%d", minutes);
  snprintf(right, sizeof(right), "%02d", seconds);
  fmtColonAnchored(out, left, right, 2, true);
}

void fmtSecTenthsAnchored(char* out, int seconds, int tenths) {
  char left[8], right[8];
  snprintf(left, sizeof(left), "%d", seconds);
  snprintf(right, sizeof(right), "%d", tenths);
  fmtColonAnchored(out, left, right, 1, true);
}

}  // namespace

// -- Countdown -----------------------------------------------------------------
// Padding rules applied below:
//   dd      -> formats 0-3 use compact d suffix; formats 4-7 right-justify
//   hh      -> fmtZeroPadded before ':', fmtBlankPadded otherwise
//   mm, ss  -> %02d after ':', else %2d
//   u       -> single digit
//   hhh (formats 10-11) -> no days row; f.days * 24 + f.hours instead, so a
//                        multi-day remaining time doesn't wrap the hour
//                        count back down once days would otherwise absorb it

namespace {

struct CountingRenderContext {
  const TimeFields& f;
  int totalHours;
  const char* hh;
  const char* hhColon;
};

struct ClockRenderContext {
  const TimeFields& f;
  const char* dow;
  bool colonVisible;
};

using CountingRenderer = void (*)(const CountingRenderContext&, char*, char*, char*);
using ClockRenderer = void (*)(const ClockRenderContext&, char*, char*, char*);

void renderCountFmt0(const CountingRenderContext& c, char* r1, char* r2, char* r3) {
  fmtDaysWithLabel(r1, c.f.days);
  snprintf(r2, 8, "%s:%02d", c.hhColon, c.f.minutes);
  fmtSecTenthsAnchored(r3, c.f.seconds, c.f.tenths);
}

void renderCountFmt1(const CountingRenderContext& c, char* r1, char* r2, char* r3) {
  fmtDaysWithLabel(r1, c.f.days);
  snprintf(r2, 8, "%s:%02d", c.hhColon, c.f.minutes);
  fmtNumber(r3, c.f.seconds);
}

void renderCountFmt2(const CountingRenderContext& c, char* r1, char* r2, char* r3) {
  fmtDaysWithLabel(r1, c.f.days);
  fmtValueWithLabel(r2, c.hh, kHourLabel);
  fmtMinSec(r3, c.f.minutes, c.f.seconds);
}

void renderCountFmt3(const CountingRenderContext& c, char* r1, char* r2, char* r3) {
  fmtDaysWithLabel(r1, c.f.days);
  fmtValueWithLabel(r2, c.hh, kHourLabel);
  fmtIntWithLabel(r3, c.f.minutes, kMinuteLabel);
}

void renderCountFmt4(const CountingRenderContext& c, char* r1, char* r2, char* r3) {
  fmtDaysRight(r1, c.f.days);
  snprintf(r2, 8, "%s:%02d", c.hhColon, c.f.minutes);
  fmtSecTenthsAnchored(r3, c.f.seconds, c.f.tenths);
}

void renderCountFmt5(const CountingRenderContext& c, char* r1, char* r2, char* r3) {
  fmtDaysRight(r1, c.f.days);
  snprintf(r2, 8, "%s:%02d", c.hhColon, c.f.minutes);
  fmtNumber(r3, c.f.seconds);
}

void renderCountFmt6(const CountingRenderContext& c, char* r1, char* r2, char* r3) {
  fmtDaysRight(r1, c.f.days);
  fmtText(r2, c.hh);
  fmtMinSec(r3, c.f.minutes, c.f.seconds);
}

void renderCountFmt7(const CountingRenderContext& c, char* r1, char* r2, char* r3) {
  fmtDaysRight(r1, c.f.days);
  fmtText(r2, c.hh);
  fmtNumber(r3, c.f.minutes);
}

void renderCountFmt8(const CountingRenderContext& c, char* r1, char* r2, char* r3) {
  fmtValueWithLabel(r1, c.hh, kHourLabel);
  fmtIntWithLabel(r2, c.f.minutes, kMinuteLabel);
  fmtSecTenthsAnchored(r3, c.f.seconds, c.f.tenths);
}

void renderCountFmt9(const CountingRenderContext& c, char* r1, char* r2, char* r3) {
  fmtValueWithLabel(r1, c.hh, kHourLabel);
  fmtIntWithLabel(r2, c.f.minutes, kMinuteLabel);
  fmtNumber(r3, c.f.seconds);
}

void renderCountFmt10(const CountingRenderContext& c, char* r1, char* r2, char* r3) {
  fmtNumber(r1, c.totalHours);
  fmtNumber(r2, c.f.minutes);
  fmtSecTenthsAnchored(r3, c.f.seconds, c.f.tenths);
}

void renderCountFmt11(const CountingRenderContext& c, char* r1, char* r2, char* r3) {
  fmtNumber(r1, c.totalHours);
  fmtNumber(r2, c.f.minutes);
  fmtNumber(r3, c.f.seconds);
}

void renderCountFmt12(const CountingRenderContext& c, char* r1, char* r2, char* r3) {
  fmtText(r1, "    ");
  fmtHourMin(r2, c.totalHours, c.f.minutes);
  fmtSecTenthsAnchored(r3, c.f.seconds, c.f.tenths);
}

void renderCountFmt13(const CountingRenderContext& c, char* r1, char* r2, char* r3) {
  fmtText(r1, "    ");
  fmtHourMin(r2, c.totalHours, c.f.minutes);
  fmtNumber(r3, c.f.seconds);
}

const CountingRenderer kCountingRenderers[] = {
  renderCountFmt0,
  renderCountFmt1,
  renderCountFmt2,
  renderCountFmt3,
  renderCountFmt4,
  renderCountFmt5,
  renderCountFmt6,
  renderCountFmt7,
  renderCountFmt8,
  renderCountFmt9,
  renderCountFmt10,
  renderCountFmt11,
  renderCountFmt12,
  renderCountFmt13,
};

void renderClockFmt0(const ClockRenderContext& c, char* r1, char* r2, char* r3) {
  fmtText(r1, c.dow);
  fmtMonthDay(r2, c.f.month, c.f.dayOfMonth);
  fmtHourMinBlink(r3, c.f.hours, c.f.minutes, c.colonVisible);
}

void renderClockFmt1(const ClockRenderContext& c, char* r1, char* r2, char* r3) {
  fmtText(r1, c.dow);
  fmtText(r2, "    ");
  fmtHourMinBlink(r3, c.f.hours, c.f.minutes, c.colonVisible);
}

void renderClockFmt2(const ClockRenderContext& c, char* r1, char* r2, char* r3) {
  fmtText(r1, "    ");
  fmtText(r2, c.dow);
  fmtHourMinBlink(r3, c.f.hours, c.f.minutes, c.colonVisible);
}

void renderClockFmt3(const ClockRenderContext& c, char* r1, char* r2, char* r3) {
  fmtText(r1, c.dow);
  fmtNumber(r2, c.f.month);
  fmtNumber(r3, c.f.dayOfMonth);
}

void renderClockFmt4(const ClockRenderContext& c, char* r1, char* r2, char* r3) {
  fmtText(r1, c.dow);
  fmtHourMin(r2, c.f.hours, c.f.minutes);
  snprintf(r3, 8, "%2d:%d", c.f.seconds, c.f.tenths);
}

void renderClockFmt5(const ClockRenderContext& c, char* r1, char* r2, char* r3) {
  fmtText(r1, c.dow);
  fmtHourMin(r2, c.f.hours, c.f.minutes);
  fmtNumber(r3, c.f.seconds);
}

void renderClockFmt6(const ClockRenderContext& c, char* r1, char* r2, char* r3) {
  fmtText(r1, c.dow);
  fmtIntWithLabel(r2, c.f.hours, kHourLabel);
  fmtMinSec(r3, c.f.minutes, c.f.seconds);
}

void renderClockFmt7(const ClockRenderContext& c, char* r1, char* r2, char* r3) {
  fmtText(r1, c.dow);
  fmtIntWithLabel(r2, c.f.hours, kHourLabel);
  fmtIntWithLabel(r3, c.f.minutes, kMinuteLabel);
}

void renderClockFmt8(const ClockRenderContext& c, char* r1, char* r2, char* r3) {
  snprintf(r1, 8, "%4d", c.f.year);
  fmtMonthDay(r2, c.f.month, c.f.dayOfMonth);
  fmtHourMinBlink(r3, c.f.hours, c.f.minutes, c.colonVisible);
}

void renderClockFmt9(const ClockRenderContext& c, char* r1, char* r2, char* r3) {
  snprintf(r1, 8, "%4d", c.f.year);
  fmtNumber(r2, c.f.month);
  fmtNumber(r3, c.f.dayOfMonth);
}

void renderClockFmt10(const ClockRenderContext& c, char* r1, char* r2, char* r3) {
  fmtNumber(r1, c.f.month);
  fmtNumber(r2, c.f.dayOfMonth);
  fmtHourMinBlink(r3, c.f.hours, c.f.minutes, c.colonVisible);
}

void renderClockFmt11(const ClockRenderContext& c, char* r1, char* r2, char* r3) {
  fmtMonthDay(r1, c.f.month, c.f.dayOfMonth);
  fmtHourMin(r2, c.f.hours, c.f.minutes);
  fmtSecTenthsAnchored(r3, c.f.seconds, c.f.tenths);
}

void renderClockFmt12(const ClockRenderContext& c, char* r1, char* r2, char* r3) {
  fmtMonthDay(r1, c.f.month, c.f.dayOfMonth);
  fmtHourMin(r2, c.f.hours, c.f.minutes);
  fmtNumber(r3, c.f.seconds);
}

void renderClockFmt13(const ClockRenderContext& c, char* r1, char* r2, char* r3) {
  fmtMonthDay(r1, c.f.month, c.f.dayOfMonth);
  fmtNumber(r2, c.f.hours);
  fmtMinSec(r3, c.f.minutes, c.f.seconds);
}

void renderClockFmt14(const ClockRenderContext& c, char* r1, char* r2, char* r3) {
  fmtMonthDay(r1, c.f.month, c.f.dayOfMonth);
  fmtNumber(r2, c.f.hours);
  fmtNumber(r3, c.f.minutes);
}

void renderClockFmt15(const ClockRenderContext& c, char* r1, char* r2, char* r3) {
  fmtNumber(r1, c.f.dayOfMonth);
  fmtHourMin(r2, c.f.hours, c.f.minutes);
  fmtSecTenthsAnchored(r3, c.f.seconds, c.f.tenths);
}

void renderClockFmt16(const ClockRenderContext& c, char* r1, char* r2, char* r3) {
  fmtNumber(r1, c.f.dayOfMonth);
  fmtHourMin(r2, c.f.hours, c.f.minutes);
  fmtNumber(r3, c.f.seconds);
}

void renderClockFmt17(const ClockRenderContext& c, char* r1, char* r2, char* r3) {
  fmtNumber(r1, c.f.dayOfMonth);
  fmtNumber(r2, c.f.hours);
  fmtMinSec(r3, c.f.minutes, c.f.seconds);
}

void renderClockFmt18(const ClockRenderContext& c, char* r1, char* r2, char* r3) {
  fmtNumber(r1, c.f.dayOfMonth);
  fmtNumber(r2, c.f.hours);
  fmtNumber(r3, c.f.minutes);
}

const ClockRenderer kClockRenderers[] = {
  renderClockFmt0,
  renderClockFmt1,
  renderClockFmt2,
  renderClockFmt3,
  renderClockFmt4,
  renderClockFmt5,
  renderClockFmt6,
  renderClockFmt7,
  renderClockFmt8,
  renderClockFmt9,
  renderClockFmt10,
  renderClockFmt11,
  renderClockFmt12,
  renderClockFmt13,
  renderClockFmt14,
  renderClockFmt15,
  renderClockFmt16,
  renderClockFmt17,
  renderClockFmt18,
};

}  // namespace

void renderCountdown(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3) {
  const int totalHours = f.days * 24 + f.hours;
  const uint8_t effectiveIdx = resolveCountingOverflowIndex(idx, totalHours);

  char hh[4], hhColon[4];
  fmtBlankPadded(hh, f.hours);
  fmtZeroPadded(hhColon, f.hours);

  const uint8_t safeIdx = (effectiveIdx < static_cast<uint8_t>(sizeof(kCountingRenderers) / sizeof(kCountingRenderers[0])))
                              ? effectiveIdx
                              : 0;
  const CountingRenderContext context{f, totalHours, hh, hhColon};
  kCountingRenderers[safeIdx](context, r1, r2, r3);

  clockFormatDebugLog("count idx=%u effective=%u rows='%s'/'%s'/'%s'", idx, safeIdx, r1, r2, r3);
}

void renderCountup(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3) {
  renderCountdown(idx, f, r1, r2, r3);
}

// -- Clock ---------------------------------------------------------------------
// All calendar fields (YYYY, MM, DD) are zero-padded - they are real dates,
// not elapsed values, so "0 hours" suppression does not apply.
// hh:mm uses %2d so midnight shows " 0:00", right-justified to the colon.

void renderClock(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3, bool colonVisible) {
  const uint8_t safeIdx = (idx < static_cast<uint8_t>(sizeof(kClockRenderers) / sizeof(kClockRenderers[0])))
                              ? idx
                              : 0;
  const ClockRenderContext context{f, dowAbbrev(f.dayOfWeek), colonVisible};
  kClockRenderers[safeIdx](context, r1, r2, r3);

  clockFormatDebugLog("clock idx=%u effective=%u rows='%s'/'%s'/'%s'", idx, safeIdx, r1, r2, r3);
}
