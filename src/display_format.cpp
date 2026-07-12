#include "display_format.h"

#include <stdio.h>
#include <string.h>

namespace {

constexpr char kDayLabel = 'd';
constexpr char kHourLabel = 'h';
constexpr char kMinuteLabel = 'n';
constexpr uint8_t kNoFallback = UINT8_MAX;

struct RenderValues {
  int year = 0;
  int month = 0;
  int day = 0;
  int dayOfWeek = 0;
  int days = 0;
  int hours = 0;
  int totalHours = 0;
  int minutes = 0;
  int seconds = 0;
  int tenths = 0;
  bool colonVisible = true;
};

using PanelRenderer = void (*)(const RenderValues&, char*);

struct FormatSpec {
  DisplayFormatInfo info;
  PanelRenderer panels[kDisplayPanelCount];
  uint8_t overflowFallback = kNoFallback;
};

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
    } else {
      output[out++] = c;
      justWroteSpace = false;
    }
  }
  while (out > 0 && output[out - 1] == ' ') --out;
  output[out] = '\0';
}

void rightmostSlice(const char* input, size_t width,
                    char* output, size_t outputSize) {
  if (outputSize == 0) return;
  output[0] = '\0';
  if (input == nullptr) return;
  const size_t length = strlen(input);
  snprintf(output, outputSize, "%s",
           length > width ? input + length - width : input);
}

void formatRight(char* out, const char* raw) {
  char normalized[16];
  char sliced[8];
  normalizeSpaces(raw, normalized, sizeof(normalized));
  rightmostSlice(normalized, 4, sliced, sizeof(sliced));
  snprintf(out, kDisplayFramePanelSize, "%4s", sliced);
}

void formatColon(char* out, const char* leftRaw, const char* rightRaw,
                 uint8_t rightWidth, bool colonVisible) {
  char leftNormalized[16];
  char rightNormalized[16];
  char left[8];
  char right[8];
  normalizeSpaces(leftRaw, leftNormalized, sizeof(leftNormalized));
  normalizeSpaces(rightRaw, rightNormalized, sizeof(rightNormalized));
  rightmostSlice(leftNormalized, 2, left, sizeof(left));
  rightmostSlice(rightNormalized, rightWidth, right, sizeof(right));

  char composed[8];
  size_t position = 0;
  const size_t leftLength = strlen(left);
  composed[position++] = leftLength >= 2 ? left[leftLength - 2] : ' ';
  composed[position++] = leftLength >= 1 ? left[leftLength - 1] : ' ';
  if (colonVisible) composed[position++] = ':';
  const size_t rightLength = strlen(right);
  for (uint8_t i = 0; i < rightWidth; ++i) {
    composed[position++] = i < rightLength ? right[i] : ' ';
  }
  composed[position] = '\0';
  snprintf(out, kDisplayFramePanelSize, "%s", composed);
}

void formatNumber(char* out, int value) {
  snprintf(out, kDisplayFramePanelSize, "%4d", value);
}

void formatValueWithLabel(char* out, const char* value, char label) {
  char raw[12];
  snprintf(raw, sizeof(raw), "%s %c", value == nullptr ? "" : value, label);
  formatRight(out, raw);
}

void formatIntWithLabel(char* out, int value, char label) {
  char text[8];
  snprintf(text, sizeof(text), "%d", value);
  formatValueWithLabel(out, text, label);
}

void formatHourMinute(char* out, int hours, int minutes, bool colonVisible) {
  char left[8];
  char right[8];
  snprintf(left, sizeof(left), "%d", hours);
  snprintf(right, sizeof(right), "%02d", minutes);
  formatColon(out, left, right, 2, colonVisible);
}

void formatMinuteSecond(char* out, int minutes, int seconds) {
  char left[8];
  char right[8];
  snprintf(left, sizeof(left), "%d", minutes);
  snprintf(right, sizeof(right), "%02d", seconds);
  formatColon(out, left, right, 2, true);
}

void formatSecondTenths(char* out, int seconds, int tenths) {
  char left[8];
  char right[8];
  snprintf(left, sizeof(left), "%d", seconds);
  snprintf(right, sizeof(right), "%d", tenths);
  formatColon(out, left, right, 1, true);
}

const char* dayOfWeekAbbreviation(int dayOfWeek) {
  static const char* const kNames[] = {
      "Sun", "non", "tu", "uEd", "thu", "Fri", "Sat"};
  return dayOfWeek >= 0 && dayOfWeek < 7 ? kNames[dayOfWeek] : "   ";
}

void renderBlank(const RenderValues&, char* out) { formatRight(out, ""); }
void renderDaysRight(const RenderValues& v, char* out) {
  formatNumber(out, constrain(v.days, 0, 9999));
}
void renderDaysWithLabel(const RenderValues& v, char* out) {
  const int days = constrain(v.days, 0, 9999);
  if (days < 10) snprintf(out, kDisplayFramePanelSize, " %d %c", days, kDayLabel);
  else if (days < 100) snprintf(out, kDisplayFramePanelSize, "%2d %c", days, kDayLabel);
  else if (days < 1000) snprintf(out, kDisplayFramePanelSize, "%3d%c", days, kDayLabel);
  else snprintf(out, kDisplayFramePanelSize, "%4d", days);
}
void renderHoursLabel(const RenderValues& v, char* out) {
  char hours[4];
  if (v.hours == 0) snprintf(hours, sizeof(hours), "  ");
  else snprintf(hours, sizeof(hours), "%2d", v.hours);
  formatValueWithLabel(out, hours, kHourLabel);
}
void renderMinutesLabel(const RenderValues& v, char* out) {
  formatIntWithLabel(out, v.minutes, kMinuteLabel);
}
void renderTotalHours(const RenderValues& v, char* out) {
  formatNumber(out, v.totalHours);
}
void renderHourMinute(const RenderValues& v, char* out) {
  char hours[4];
  snprintf(hours, sizeof(hours), "%2d", v.hours);
  snprintf(out, kDisplayFramePanelSize, "%s:%02d", hours, v.minutes);
}
void renderTotalHourMinute(const RenderValues& v, char* out) {
  formatHourMinute(out, v.totalHours, v.minutes, true);
}
void renderHoursNumberBlankZero(const RenderValues& v, char* out) {
  char hours[4];
  if (v.hours == 0) snprintf(hours, sizeof(hours), "  ");
  else snprintf(hours, sizeof(hours), "%2d", v.hours);
  formatRight(out, hours);
}
void renderMinuteSecond(const RenderValues& v, char* out) {
  formatMinuteSecond(out, v.minutes, v.seconds);
}
void renderSecondTenths(const RenderValues& v, char* out) {
  formatSecondTenths(out, v.seconds, v.tenths);
}
void renderSecondTenthsCompact(const RenderValues& v, char* out) {
  snprintf(out, kDisplayFramePanelSize, "%2d:%d", v.seconds, v.tenths);
}
void renderSeconds(const RenderValues& v, char* out) { formatNumber(out, v.seconds); }
void renderMinutes(const RenderValues& v, char* out) { formatNumber(out, v.minutes); }
void renderDayOfWeek(const RenderValues& v, char* out) {
  formatRight(out, dayOfWeekAbbreviation(v.dayOfWeek));
}
void renderMonthDay(const RenderValues& v, char* out) {
  char month[8];
  char day[8];
  snprintf(month, sizeof(month), "%d", v.month);
  snprintf(day, sizeof(day), "%02d", v.day);
  formatColon(out, month, day, 2, true);
}
void renderBlinkingHourMinute(const RenderValues& v, char* out) {
  formatHourMinute(out, v.hours, v.minutes, v.colonVisible);
}
void renderFixedHourMinute(const RenderValues& v, char* out) {
  formatHourMinute(out, v.hours, v.minutes, true);
}
void renderMonth(const RenderValues& v, char* out) { formatNumber(out, v.month); }
void renderDay(const RenderValues& v, char* out) { formatNumber(out, v.day); }
void renderYear(const RenderValues& v, char* out) { formatNumber(out, v.year); }
void renderHours(const RenderValues& v, char* out) { formatNumber(out, v.hours); }

const FormatSpec kCountingFormats[] = {
  {{"dd D |  hh:mm |  ss:u", true, false}, {renderDaysWithLabel, renderHourMinute, renderSecondTenths}, kNoFallback},
  {{"dd D |  hh:mm |    ss", false, false}, {renderDaysWithLabel, renderHourMinute, renderSeconds}, kNoFallback},
  {{"dd D |  hh  H | mm:ss", false, false}, {renderDaysWithLabel, renderHoursLabel, renderMinuteSecond}, kNoFallback},
  {{"dd D |  hh  H |  mm N", false, false}, {renderDaysWithLabel, renderHoursLabel, renderMinutesLabel}, kNoFallback},
  {{"  dd |  hh:mm |  ss:u", true, false}, {renderDaysRight, renderHourMinute, renderSecondTenths}, kNoFallback},
  {{"  dd |  hh:mm |    ss", false, false}, {renderDaysRight, renderHourMinute, renderSeconds}, kNoFallback},
  {{"  dd |     hh | mm:ss", false, false}, {renderDaysRight, renderHoursNumberBlankZero, renderMinuteSecond}, kNoFallback},
  {{"  dd |     hh |    mm", false, false}, {renderDaysRight, renderHoursNumberBlankZero, renderMinutes}, kNoFallback},
  {{"hh H |   mm N |  ss:u", true, false}, {renderHoursLabel, renderMinutesLabel, renderSecondTenths}, kNoFallback},
  {{"hh H |   mm N |    ss", false, false}, {renderHoursLabel, renderMinutesLabel, renderSeconds}, kNoFallback},
  {{" hhh |     mm |  ss:u", true, false}, {renderTotalHours, renderMinutes, renderSecondTenths}, kNoFallback},
  {{" hhh |     mm |    ss", false, false}, {renderTotalHours, renderMinutes, renderSeconds}, kNoFallback},
  {{"     | hhh:mm |  ss:u", true, false}, {renderBlank, renderTotalHourMinute, renderSecondTenths}, 10},
  {{"     | hhh:mm |    ss", false, false}, {renderBlank, renderTotalHourMinute, renderSeconds}, 11},
};

const FormatSpec kClockFormats[] = {
  {{" DOW  | MM:DD | hh;mm", false, true}, {renderDayOfWeek, renderMonthDay, renderBlinkingHourMinute}, kNoFallback},
  {{" DOW  |       | hh;mm", false, true}, {renderDayOfWeek, renderBlank, renderBlinkingHourMinute}, kNoFallback},
  {{"      |   DOW | hh;mm", false, true}, {renderBlank, renderDayOfWeek, renderBlinkingHourMinute}, kNoFallback},
  {{" DOW  |    MM |    DD", false, false}, {renderDayOfWeek, renderMonth, renderDay}, kNoFallback},
  {{" DOW  | hh:mm |  ss:u", true, false}, {renderDayOfWeek, renderFixedHourMinute, renderSecondTenthsCompact}, kNoFallback},
  {{" DOW  | hh:mm |    ss", false, false}, {renderDayOfWeek, renderFixedHourMinute, renderSeconds}, kNoFallback},
  {{" DOW  | hh  H | mm:ss", false, false}, {renderDayOfWeek, renderHoursLabel, renderMinuteSecond}, kNoFallback},
  {{" DOW  | hh  H |  mm N", false, false}, {renderDayOfWeek, renderHoursLabel, renderMinutesLabel}, kNoFallback},
  {{" YYYY | MM:DD | hh;mm", false, true}, {renderYear, renderMonthDay, renderBlinkingHourMinute}, kNoFallback},
  {{" YYYY |    MM |    DD", false, false}, {renderYear, renderMonth, renderDay}, kNoFallback},
  {{"   MM |    DD | hh;mm", false, true}, {renderMonth, renderDay, renderBlinkingHourMinute}, kNoFallback},
  {{"MM:DD | hh:mm |  ss:u", true, false}, {renderMonthDay, renderFixedHourMinute, renderSecondTenths}, kNoFallback},
  {{"MM:DD | hh:mm |    ss", false, false}, {renderMonthDay, renderFixedHourMinute, renderSeconds}, kNoFallback},
  {{"MM:DD |    hh | mm:ss", false, false}, {renderMonthDay, renderHours, renderMinuteSecond}, kNoFallback},
  {{"MM:DD |    hh |    mm", false, false}, {renderMonthDay, renderHours, renderMinutes}, kNoFallback},
  {{"   DD | hh:mm |  ss:u", true, false}, {renderDay, renderFixedHourMinute, renderSecondTenths}, kNoFallback},
  {{"   DD | hh:mm |    ss", false, false}, {renderDay, renderFixedHourMinute, renderSeconds}, kNoFallback},
  {{"   DD |    hh | mm:ss", false, false}, {renderDay, renderHours, renderMinuteSecond}, kNoFallback},
  {{"   DD |    hh |    mm", false, false}, {renderDay, renderHours, renderMinutes}, kNoFallback},
};

constexpr uint8_t kCountingFormatCount = sizeof(kCountingFormats) / sizeof(kCountingFormats[0]);
constexpr uint8_t kClockFormatCount = sizeof(kClockFormats) / sizeof(kClockFormats[0]);

const FormatSpec& safeFormat(FormatGroup group, uint8_t index) {
  if (group == kFmtGroupClock) {
    return kClockFormats[index < kClockFormatCount ? index : 0];
  }
  return kCountingFormats[index < kCountingFormatCount ? index : 0];
}

DisplayFrame renderPanels(const FormatSpec& format, const RenderValues& values) {
  DisplayFrame frame;
  for (uint8_t panel = 0; panel < kDisplayPanelCount; ++panel) {
    format.panels[panel](values, frame.panels[panel]);
  }
  return frame;
}

RenderValues valuesFromDuration(long totalSeconds, uint8_t tenths) {
  if (totalSeconds < 0) totalSeconds = 0;
  RenderValues values;
  values.days = totalSeconds / 86400;
  totalSeconds %= 86400;
  values.hours = totalSeconds / 3600;
  totalSeconds %= 3600;
  values.minutes = totalSeconds / 60;
  values.seconds = totalSeconds % 60;
  values.totalHours = values.days * 24 + values.hours;
  values.tenths = tenths;
  return values;
}

RenderValues valuesFromClock(const DateTime& now, bool use12Hour,
                             uint8_t tenths, bool colonVisible) {
  RenderValues values;
  values.year = now.year();
  values.month = now.month();
  values.day = now.day();
  values.dayOfWeek = now.dayOfTheWeek();
  values.hours = now.hour();
  if (use12Hour) {
    values.hours %= 12;
    if (values.hours == 0) values.hours = 12;
  }
  values.minutes = now.minute();
  values.seconds = now.second();
  values.tenths = tenths;
  values.colonVisible = colonVisible;
  return values;
}

}  // namespace

uint8_t displayFormatCount(FormatGroup group) {
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

const DisplayFormatInfo& displayFormatInfo(FormatGroup group, uint8_t index) {
  return safeFormat(group, index).info;
}

DisplayFrame renderCountingFormat(uint8_t index, long totalSeconds,
                                  uint8_t tenths) {
  const RenderValues values = valuesFromDuration(totalSeconds, tenths);
  const FormatSpec* format = &safeFormat(kFmtGroupCountdown, index);
  if (values.totalHours > 99 && format->overflowFallback != kNoFallback) {
    format = &kCountingFormats[format->overflowFallback];
  }
  return renderPanels(*format, values);
}

DisplayFrame renderClockFormat(uint8_t index, const DateTime& now,
                               bool use12Hour, uint8_t tenths,
                               bool colonVisible) {
  return renderPanels(safeFormat(kFmtGroupClock, index),
                    valuesFromClock(now, use12Hour, tenths, colonVisible));
}
