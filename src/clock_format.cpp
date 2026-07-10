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
// Counting output is produced by the table-driven format catalog (kCountingFormats).
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

enum class CountingSegment : uint8_t {
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

// One catalog entry: the label shown by the web configurator plus the three
// segments that produce rows r1/r2/r3. Label and behavior can't drift
// apart because they live in the same table entry.
struct CountingFormat {
  const char* label;
  CountingSegment segments[3];
};

void renderCountingRow(CountingSegment op, const CountingRenderContext& c, char* out) {
  switch (op) {
    case CountingSegment::kDaysWithLabel:
      fmtDaysWithLabel(out, c.f.days);
      return;
    case CountingSegment::kDaysRight:
      fmtDaysRight(out, c.f.days);
      return;
    case CountingSegment::kHoursLabel:
      fmtValueWithLabel(out, c.hh, kHourLabel);
      return;
    case CountingSegment::kMinutesLabel:
      fmtIntWithLabel(out, c.f.minutes, kMinuteLabel);
      return;
    case CountingSegment::kTotalHours:
      fmtNumber(out, c.totalHours);
      return;
    case CountingSegment::kHourMin:
      snprintf(out, 8, "%s:%02d", c.hhColon, c.f.minutes);
      return;
    case CountingSegment::kTotalHourMin:
      fmtHourMin(out, c.totalHours, c.f.minutes);
      return;
    case CountingSegment::kHoursText:
      fmtText(out, c.hh);
      return;
    case CountingSegment::kMinSec:
      fmtMinSec(out, c.f.minutes, c.f.seconds);
      return;
    case CountingSegment::kSecondsTenths:
      fmtSecTenthsAnchored(out, c.f.seconds, c.f.tenths);
      return;
    case CountingSegment::kSeconds:
      fmtNumber(out, c.f.seconds);
      return;
    case CountingSegment::kMinutes:
      fmtNumber(out, c.f.minutes);
      return;
    case CountingSegment::kBlank:
      fmtText(out, "    ");
      return;
  }
}

// -- Counting formats (shared by Countdown + Count-Up) --------------------------
// Label tokens: dd=days  hh/mm/ss=time  u=tenths
// Label literals: D=days-label  H=hours-label  N=minutes-label
// hhh: total hours (days * 24 + hours) - no separate days row; accumulates
// past 24. Any format combining hhh:mm on one 4-digit row only works through
// 99:59; above 99 hours, rendering auto-falls back to a compatible split
// hhh | mm variant (see resolveCountingOverflowIndex).
const CountingFormat kCountingFormats[] = {
  {"dd D |  hh:mm |  ss:u",
     {CountingSegment::kDaysWithLabel, CountingSegment::kHourMin,      CountingSegment::kSecondsTenths}},
  {"dd D |  hh:mm |    ss",
     {CountingSegment::kDaysWithLabel, CountingSegment::kHourMin,      CountingSegment::kSeconds}},
  {"dd D |  hh  H | mm:ss",
     {CountingSegment::kDaysWithLabel, CountingSegment::kHoursLabel,   CountingSegment::kMinSec}},
  {"dd D |  hh  H |  mm N",
     {CountingSegment::kDaysWithLabel, CountingSegment::kHoursLabel,   CountingSegment::kMinutesLabel}},
  {"  dd |  hh:mm |  ss:u",
     {CountingSegment::kDaysRight,     CountingSegment::kHourMin,      CountingSegment::kSecondsTenths}},
  {"  dd |  hh:mm |    ss",
     {CountingSegment::kDaysRight,     CountingSegment::kHourMin,      CountingSegment::kSeconds}},
  {"  dd |     hh | mm:ss",
     {CountingSegment::kDaysRight,     CountingSegment::kHoursText,    CountingSegment::kMinSec}},
  {"  dd |     hh |    mm",
     {CountingSegment::kDaysRight,     CountingSegment::kHoursText,    CountingSegment::kMinutes}},
  {"hh H |   mm N |  ss:u",
     {CountingSegment::kHoursLabel,    CountingSegment::kMinutesLabel, CountingSegment::kSecondsTenths}},
  {"hh H |   mm N |    ss",
     {CountingSegment::kHoursLabel,    CountingSegment::kMinutesLabel, CountingSegment::kSeconds}},
  {" hhh |     mm |  ss:u",
     {CountingSegment::kTotalHours,    CountingSegment::kMinutes,      CountingSegment::kSecondsTenths}},
  {" hhh |     mm |    ss",
     {CountingSegment::kTotalHours,    CountingSegment::kMinutes,      CountingSegment::kSeconds}},
  {"     | hhh:mm |  ss:u",
     {CountingSegment::kBlank,         CountingSegment::kTotalHourMin, CountingSegment::kSecondsTenths}},
  {"     | hhh:mm |    ss",
     {CountingSegment::kBlank,         CountingSegment::kTotalHourMin, CountingSegment::kSeconds}},
};

constexpr uint8_t kCountingFormatCount =
    static_cast<uint8_t>(sizeof(kCountingFormats) / sizeof(kCountingFormats[0]));

enum class ClockSegment : uint8_t {
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

struct ClockFormat {
  const char* label;
  ClockSegment segments[3];
};

void renderClockRow(ClockSegment op, const ClockRenderContext& c, char* out) {
  switch (op) {
    case ClockSegment::kDow:
      fmtText(out, c.dow);
      return;
    case ClockSegment::kBlank:
      fmtText(out, "    ");
      return;
    case ClockSegment::kMonthDay:
      fmtMonthDay(out, c.f.month, c.f.dayOfMonth);
      return;
    case ClockSegment::kHourMinBlink:
      fmtHourMinBlink(out, c.f.hours, c.f.minutes, c.colonVisible);
      return;
    case ClockSegment::kMonthNumber:
      fmtNumber(out, c.f.month);
      return;
    case ClockSegment::kDayNumber:
      fmtNumber(out, c.f.dayOfMonth);
      return;
    case ClockSegment::kHourMin:
      fmtHourMin(out, c.f.hours, c.f.minutes);
      return;
    case ClockSegment::kSecondsTenthsCompact:
      snprintf(out, 8, "%2d:%d", c.f.seconds, c.f.tenths);
      return;
    case ClockSegment::kSecondsTenthsAnchored:
      fmtSecTenthsAnchored(out, c.f.seconds, c.f.tenths);
      return;
    case ClockSegment::kSeconds:
      fmtNumber(out, c.f.seconds);
      return;
    case ClockSegment::kHoursLabel:
      fmtIntWithLabel(out, c.f.hours, kHourLabel);
      return;
    case ClockSegment::kMinSec:
      fmtMinSec(out, c.f.minutes, c.f.seconds);
      return;
    case ClockSegment::kMinutesLabel:
      fmtIntWithLabel(out, c.f.minutes, kMinuteLabel);
      return;
    case ClockSegment::kYear:
      snprintf(out, 8, "%4d", c.f.year);
      return;
    case ClockSegment::kHoursNumber:
      fmtNumber(out, c.f.hours);
      return;
    case ClockSegment::kMinutesNumber:
      fmtNumber(out, c.f.minutes);
      return;
  }
}

// -- Clock formats ---------------------------------------------------------------
// Label tokens: YYYY=year  MM=month  DD=day-of-month  DOW=day-of-week
//               hh/mm/ss=time  u=tenths
// Label literals: H=hours-label  N=minutes-label
// A semi-colon in hh;mm marks the colon as blinking (kHourMinBlink).
const ClockFormat kClockFormats[] = {
  {" DOW  | MM:DD | hh;mm",
     {ClockSegment::kDow,         ClockSegment::kMonthDay,    ClockSegment::kHourMinBlink}},
  {" DOW  |       | hh;mm",
     {ClockSegment::kDow,         ClockSegment::kBlank,       ClockSegment::kHourMinBlink}},
  {"      |   DOW | hh;mm",
     {ClockSegment::kBlank,       ClockSegment::kDow,         ClockSegment::kHourMinBlink}},
  {" DOW  |    MM |    DD",
     {ClockSegment::kDow,         ClockSegment::kMonthNumber, ClockSegment::kDayNumber}},
  {" DOW  | hh:mm |  ss:u",
     {ClockSegment::kDow,         ClockSegment::kHourMin,     ClockSegment::kSecondsTenthsCompact}},
  {" DOW  | hh:mm |    ss",
     {ClockSegment::kDow,         ClockSegment::kHourMin,     ClockSegment::kSeconds}},
  {" DOW  | hh  H | mm:ss",
     {ClockSegment::kDow,         ClockSegment::kHoursLabel,  ClockSegment::kMinSec}},
  {" DOW  | hh  H |  mm N",
     {ClockSegment::kDow,         ClockSegment::kHoursLabel,  ClockSegment::kMinutesLabel}},
  {" YYYY | MM:DD | hh;mm",
     {ClockSegment::kYear,        ClockSegment::kMonthDay,    ClockSegment::kHourMinBlink}},
  {" YYYY |    MM |    DD",
     {ClockSegment::kYear,        ClockSegment::kMonthNumber, ClockSegment::kDayNumber}},
  {"   MM |    DD | hh;mm",
     {ClockSegment::kMonthNumber, ClockSegment::kDayNumber,   ClockSegment::kHourMinBlink}},
  {"MM:DD | hh:mm |  ss:u",
     {ClockSegment::kMonthDay,    ClockSegment::kHourMin,     ClockSegment::kSecondsTenthsAnchored}},
  {"MM:DD | hh:mm |    ss",
     {ClockSegment::kMonthDay,    ClockSegment::kHourMin,     ClockSegment::kSeconds}},
  {"MM:DD |    hh | mm:ss",
     {ClockSegment::kMonthDay,    ClockSegment::kHoursNumber, ClockSegment::kMinSec}},
  {"MM:DD |    hh |    mm",
     {ClockSegment::kMonthDay,    ClockSegment::kHoursNumber, ClockSegment::kMinutesNumber}},
  {"   DD | hh:mm |  ss:u",
     {ClockSegment::kDayNumber,   ClockSegment::kHourMin,     ClockSegment::kSecondsTenthsAnchored}},
  {"   DD | hh:mm |    ss",
     {ClockSegment::kDayNumber,   ClockSegment::kHourMin,     ClockSegment::kSeconds}},
  {"   DD |    hh | mm:ss",
     {ClockSegment::kDayNumber,   ClockSegment::kHoursNumber, ClockSegment::kMinSec}},
  {"   DD |    hh |    mm",
     {ClockSegment::kDayNumber,   ClockSegment::kHoursNumber, ClockSegment::kMinutesNumber}},
};

constexpr uint8_t kClockFormatCount =
    static_cast<uint8_t>(sizeof(kClockFormats) / sizeof(kClockFormats[0]));

// True when op appears in a format's three segments.
template <typename Segment>
bool segmentsContain(const Segment (&segments)[3], Segment op) {
  return segments[0] == op || segments[1] == op || segments[2] == op;
}

bool countingHasTenths(uint8_t index) {
  return segmentsContain(kCountingFormats[index].segments, CountingSegment::kSecondsTenths);
}

bool clockHasTenths(uint8_t index) {
  return segmentsContain(kClockFormats[index].segments, ClockSegment::kSecondsTenthsCompact) ||
         segmentsContain(kClockFormats[index].segments, ClockSegment::kSecondsTenthsAnchored);
}

}  // namespace

void renderCountdown(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3) {
  const int totalHours = f.days * 24 + f.hours;
  const uint8_t effectiveIdx = resolveCountingOverflowIndex(idx, totalHours);

  char hh[4], hhColon[4];
  fmtBlankPadded(hh, f.hours);
  fmtZeroPadded(hhColon, f.hours);

  const uint8_t safeIdx = effectiveIdx < kCountingFormatCount ? effectiveIdx : 0;
  const CountingRenderContext context{f, totalHours, hh, hhColon};
  const CountingFormat& format = kCountingFormats[safeIdx];
  renderCountingRow(format.segments[0], context, r1);
  renderCountingRow(format.segments[1], context, r2);
  renderCountingRow(format.segments[2], context, r3);

  clockFormatDebugLog("count idx=%u effective=%u rows='%s'/'%s'/'%s'", idx, safeIdx, r1, r2, r3);
}

void renderCountup(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3) {
  renderCountdown(idx, f, r1, r2, r3);
}

// -- Clock ---------------------------------------------------------------------
// Clock output is produced by the table-driven format catalog (kClockFormats).
// Calendar fields are rendered as date values (not elapsed fields), and time
// fields preserve anchored-colon behavior for compact 4-character rows.

void renderClock(uint8_t idx, const TimeFields& f, char* r1, char* r2, char* r3, bool colonVisible) {
  const uint8_t safeIdx = idx < kClockFormatCount ? idx : 0;
  const ClockRenderContext context{f, dowAbbrev(f.dayOfWeek), colonVisible};
  const ClockFormat& format = kClockFormats[safeIdx];
  renderClockRow(format.segments[0], context, r1);
  renderClockRow(format.segments[1], context, r2);
  renderClockRow(format.segments[2], context, r3);

  clockFormatDebugLog("clock idx=%u effective=%u rows='%s'/'%s'/'%s'", idx, safeIdx, r1, r2, r3);
}

// -- Format catalog accessors (declared in format.h) ----------------------------

uint8_t formatCount(FormatGroup group) {
  switch (group) {
    case kFmtGroupCountdown:
    case kFmtGroupCountUp:
      return kCountingFormatCount;
    case kFmtGroupClock:
      return kClockFormatCount;
    default:
      return 0;
  }
}

const char* getFormat(FormatGroup group, uint8_t index) {
  if (index >= formatCount(group)) return nullptr;
  return group == kFmtGroupClock ? kClockFormats[index].label
                                 : kCountingFormats[index].label;
}

bool formatHasTenths(FormatGroup group, uint8_t index) {
  if (index >= formatCount(group)) return false;
  return group == kFmtGroupClock ? clockHasTenths(index) : countingHasTenths(index);
}

bool clockFormatBlinksColon(uint8_t index) {
  if (index >= kClockFormatCount) return false;
  return segmentsContain(kClockFormats[index].segments, ClockSegment::kHourMinBlink);
}

uint8_t resolveCountingOverflowIndex(uint8_t index, int totalHours) {
  if (totalHours <= 99 || index >= kCountingFormatCount) return index;
  if (!segmentsContain(kCountingFormats[index].segments, CountingSegment::kTotalHourMin)) {
    return index;
  }

  // Find a split hhh | mm variant with the same tenths behavior.
  const bool wantTenths = countingHasTenths(index);
  for (uint8_t candidate = 0; candidate < kCountingFormatCount; ++candidate) {
    if (segmentsContain(kCountingFormats[candidate].segments, CountingSegment::kTotalHours) &&
        countingHasTenths(candidate) == wantTenths) {
      return candidate;
    }
  }

  return index;
}
