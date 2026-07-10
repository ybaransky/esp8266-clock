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
// Counting output is produced by table-driven row plans (kCountingPlans).
// Padding/formatting rules are preserved from the previous per-index renderer:
//   dd      -> compact d-label variants and right-justified variants
//   hh      -> fmtZeroPadded before ':', fmtBlankPadded otherwise
//   mm, ss  -> %02d after ':', otherwise numeric right-aligned rendering
//   u       -> single tenths digit anchored with seconds
//   hhh     -> total elapsed/remaining hours (days * 24 + hours)

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

enum class CountingRowOp : uint8_t {
  kDaysWithLabel,
  kDaysRight,
  kHoursLabel,
  kMinutesLabel,
  kTotalHours,
  kHourMin,
  kTotalHourMin,
  kHoursText,
  kMinSec,
  kSecondsTenths,
  kSeconds,
  kMinutes,
  kBlank,
};

struct CountingPlan {
  CountingRowOp rows[3];
};

void renderCountingRow(CountingRowOp op, const CountingRenderContext& c, char* out) {
  switch (op) {
    case CountingRowOp::kDaysWithLabel:
      fmtDaysWithLabel(out, c.f.days);
      return;
    case CountingRowOp::kDaysRight:
      fmtDaysRight(out, c.f.days);
      return;
    case CountingRowOp::kHoursLabel:
      fmtValueWithLabel(out, c.hh, kHourLabel);
      return;
    case CountingRowOp::kMinutesLabel:
      fmtIntWithLabel(out, c.f.minutes, kMinuteLabel);
      return;
    case CountingRowOp::kTotalHours:
      fmtNumber(out, c.totalHours);
      return;
    case CountingRowOp::kHourMin:
      snprintf(out, 8, "%s:%02d", c.hhColon, c.f.minutes);
      return;
    case CountingRowOp::kTotalHourMin:
      fmtHourMin(out, c.totalHours, c.f.minutes);
      return;
    case CountingRowOp::kHoursText:
      fmtText(out, c.hh);
      return;
    case CountingRowOp::kMinSec:
      fmtMinSec(out, c.f.minutes, c.f.seconds);
      return;
    case CountingRowOp::kSecondsTenths:
      fmtSecTenthsAnchored(out, c.f.seconds, c.f.tenths);
      return;
    case CountingRowOp::kSeconds:
      fmtNumber(out, c.f.seconds);
      return;
    case CountingRowOp::kMinutes:
      fmtNumber(out, c.f.minutes);
      return;
    case CountingRowOp::kBlank:
      fmtText(out, "    ");
      return;
  }
}

const CountingPlan kCountingPlans[] = {
  {{CountingRowOp::kDaysWithLabel, CountingRowOp::kHourMin,   CountingRowOp::kSecondsTenths}},
  {{CountingRowOp::kDaysWithLabel, CountingRowOp::kHourMin,   CountingRowOp::kSeconds}},
  {{CountingRowOp::kDaysWithLabel, CountingRowOp::kHoursLabel, CountingRowOp::kMinSec}},
  {{CountingRowOp::kDaysWithLabel, CountingRowOp::kHoursLabel, CountingRowOp::kMinutesLabel}},
  {{CountingRowOp::kDaysRight,     CountingRowOp::kHourMin,   CountingRowOp::kSecondsTenths}},
  {{CountingRowOp::kDaysRight,     CountingRowOp::kHourMin,   CountingRowOp::kSeconds}},
  {{CountingRowOp::kDaysRight,     CountingRowOp::kHoursText, CountingRowOp::kMinSec}},
  {{CountingRowOp::kDaysRight,     CountingRowOp::kHoursText, CountingRowOp::kMinutes}},
  {{CountingRowOp::kHoursLabel,    CountingRowOp::kMinutesLabel, CountingRowOp::kSecondsTenths}},
  {{CountingRowOp::kHoursLabel,    CountingRowOp::kMinutesLabel, CountingRowOp::kSeconds}},
  {{CountingRowOp::kTotalHours,    CountingRowOp::kMinutes,   CountingRowOp::kSecondsTenths}},
  {{CountingRowOp::kTotalHours,    CountingRowOp::kMinutes,   CountingRowOp::kSeconds}},
  {{CountingRowOp::kBlank,         CountingRowOp::kTotalHourMin, CountingRowOp::kSecondsTenths}},
  {{CountingRowOp::kBlank,         CountingRowOp::kTotalHourMin, CountingRowOp::kSeconds}},
};

enum class ClockRowOp : uint8_t {
  kDow,
  kBlank,
  kMonthDay,
  kHourMinBlink,
  kMonthNumber,
  kDayNumber,
  kHourMin,
  kSecondsTenthsCompact,
  kSecondsTenthsAnchored,
  kSeconds,
  kHoursLabel,
  kMinSec,
  kMinutesLabel,
  kYear,
  kHoursNumber,
  kMinutesNumber,
};

struct ClockPlan {
  ClockRowOp rows[3];
};

void renderClockRow(ClockRowOp op, const ClockRenderContext& c, char* out) {
  switch (op) {
    case ClockRowOp::kDow:
      fmtText(out, c.dow);
      return;
    case ClockRowOp::kBlank:
      fmtText(out, "    ");
      return;
    case ClockRowOp::kMonthDay:
      fmtMonthDay(out, c.f.month, c.f.dayOfMonth);
      return;
    case ClockRowOp::kHourMinBlink:
      fmtHourMinBlink(out, c.f.hours, c.f.minutes, c.colonVisible);
      return;
    case ClockRowOp::kMonthNumber:
      fmtNumber(out, c.f.month);
      return;
    case ClockRowOp::kDayNumber:
      fmtNumber(out, c.f.dayOfMonth);
      return;
    case ClockRowOp::kHourMin:
      fmtHourMin(out, c.f.hours, c.f.minutes);
      return;
    case ClockRowOp::kSecondsTenthsCompact:
      snprintf(out, 8, "%2d:%d", c.f.seconds, c.f.tenths);
      return;
    case ClockRowOp::kSecondsTenthsAnchored:
      fmtSecTenthsAnchored(out, c.f.seconds, c.f.tenths);
      return;
    case ClockRowOp::kSeconds:
      fmtNumber(out, c.f.seconds);
      return;
    case ClockRowOp::kHoursLabel:
      fmtIntWithLabel(out, c.f.hours, kHourLabel);
      return;
    case ClockRowOp::kMinSec:
      fmtMinSec(out, c.f.minutes, c.f.seconds);
      return;
    case ClockRowOp::kMinutesLabel:
      fmtIntWithLabel(out, c.f.minutes, kMinuteLabel);
      return;
    case ClockRowOp::kYear:
      snprintf(out, 8, "%4d", c.f.year);
      return;
    case ClockRowOp::kHoursNumber:
      fmtNumber(out, c.f.hours);
      return;
    case ClockRowOp::kMinutesNumber:
      fmtNumber(out, c.f.minutes);
      return;
  }
}

const ClockPlan kClockPlans[] = {
  {{ClockRowOp::kDow,       ClockRowOp::kMonthDay, ClockRowOp::kHourMinBlink}},
  {{ClockRowOp::kDow,       ClockRowOp::kBlank,    ClockRowOp::kHourMinBlink}},
  {{ClockRowOp::kBlank,     ClockRowOp::kDow,      ClockRowOp::kHourMinBlink}},
  {{ClockRowOp::kDow,       ClockRowOp::kMonthNumber, ClockRowOp::kDayNumber}},
  {{ClockRowOp::kDow,       ClockRowOp::kHourMin,  ClockRowOp::kSecondsTenthsCompact}},
  {{ClockRowOp::kDow,       ClockRowOp::kHourMin,  ClockRowOp::kSeconds}},
  {{ClockRowOp::kDow,       ClockRowOp::kHoursLabel, ClockRowOp::kMinSec}},
  {{ClockRowOp::kDow,       ClockRowOp::kHoursLabel, ClockRowOp::kMinutesLabel}},
  {{ClockRowOp::kYear,      ClockRowOp::kMonthDay, ClockRowOp::kHourMinBlink}},
  {{ClockRowOp::kYear,      ClockRowOp::kMonthNumber, ClockRowOp::kDayNumber}},
  {{ClockRowOp::kMonthNumber, ClockRowOp::kDayNumber, ClockRowOp::kHourMinBlink}},
  {{ClockRowOp::kMonthDay,  ClockRowOp::kHourMin,  ClockRowOp::kSecondsTenthsAnchored}},
  {{ClockRowOp::kMonthDay,  ClockRowOp::kHourMin,  ClockRowOp::kSeconds}},
  {{ClockRowOp::kMonthDay,  ClockRowOp::kHoursNumber, ClockRowOp::kMinSec}},
  {{ClockRowOp::kMonthDay,  ClockRowOp::kHoursNumber, ClockRowOp::kMinutesNumber}},
  {{ClockRowOp::kDayNumber, ClockRowOp::kHourMin,  ClockRowOp::kSecondsTenthsAnchored}},
  {{ClockRowOp::kDayNumber, ClockRowOp::kHourMin,  ClockRowOp::kSeconds}},
  {{ClockRowOp::kDayNumber, ClockRowOp::kHoursNumber, ClockRowOp::kMinSec}},
  {{ClockRowOp::kDayNumber, ClockRowOp::kHoursNumber, ClockRowOp::kMinutesNumber}},
};

}  // namespace

void renderCountdown(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3) {
  const int totalHours = f.days * 24 + f.hours;
  const uint8_t effectiveIdx = resolveCountingOverflowIndex(idx, totalHours);

  char hh[4], hhColon[4];
  fmtBlankPadded(hh, f.hours);
  fmtZeroPadded(hhColon, f.hours);

  const uint8_t safeIdx = (effectiveIdx < static_cast<uint8_t>(sizeof(kCountingPlans) / sizeof(kCountingPlans[0])))
                              ? effectiveIdx
                              : 0;
  const CountingRenderContext context{f, totalHours, hh, hhColon};
  const CountingPlan& plan = kCountingPlans[safeIdx];
  renderCountingRow(plan.rows[0], context, r1);
  renderCountingRow(plan.rows[1], context, r2);
  renderCountingRow(plan.rows[2], context, r3);

  clockFormatDebugLog("count idx=%u effective=%u rows='%s'/'%s'/'%s'", idx, safeIdx, r1, r2, r3);
}

void renderCountup(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3) {
  renderCountdown(idx, f, r1, r2, r3);
}

// -- Clock ---------------------------------------------------------------------
// Clock output is produced by table-driven row plans (kClockPlans).
// Calendar fields are rendered as date values (not elapsed fields), and time
// fields preserve anchored-colon behavior for compact 4-character rows.

void renderClock(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3, bool colonVisible) {
  const uint8_t safeIdx = (idx < static_cast<uint8_t>(sizeof(kClockPlans) / sizeof(kClockPlans[0])))
                              ? idx
                              : 0;
  const ClockRenderContext context{f, dowAbbrev(f.dayOfWeek), colonVisible};
  const ClockPlan& plan = kClockPlans[safeIdx];
  renderClockRow(plan.rows[0], context, r1);
  renderClockRow(plan.rows[1], context, r2);
  renderClockRow(plan.rows[2], context, r3);

  clockFormatDebugLog("clock idx=%u effective=%u rows='%s'/'%s'/'%s'", idx, safeIdx, r1, r2, r3);
}

bool clockFormatValidateInvariants() {
  const uint8_t kCountingPlanCount =
      static_cast<uint8_t>(sizeof(kCountingPlans) / sizeof(kCountingPlans[0]));
  const uint8_t kClockPlanCount =
      static_cast<uint8_t>(sizeof(kClockPlans) / sizeof(kClockPlans[0]));

  bool ok = true;
  if (formatCount(kFmtGroupCountdown) != kCountingPlanCount ||
      formatCount(kFmtGroupCountUp) != kCountingPlanCount) {
    Serial.printf("clock_format: invariant failed (counting tables %u/%u vs plans %u)\n",
                  formatCount(kFmtGroupCountdown),
                  formatCount(kFmtGroupCountUp),
                  kCountingPlanCount);
    ok = false;
  }

  if (formatCount(kFmtGroupClock) != kClockPlanCount) {
    Serial.printf("clock_format: invariant failed (clock table %u vs plans %u)\n",
                  formatCount(kFmtGroupClock),
                  kClockPlanCount);
    ok = false;
  }

  return ok;
}
