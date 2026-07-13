#include "display_format.h"

#include <stdio.h>
#include <string.h>

namespace {

// Which RenderValues field a panel reads.
enum class Field : uint8_t {
  kNone = 0,
  kDays,
  kHours,
  kTotalHours,
  kMinutes,
  kSeconds,
  kTenths,
  kYear,
  kMonth,
  kDay,
  kDow,
};

// The handful of 4-character panel layouts every format is built from.
enum class Shape : uint8_t {
  kBlank,           // "    "
  kNumber,          // "  47" - right-justified
  kNumberBlankZero, // like kNumber, but blank when the value is 0
  kLabel,           // value + unit letter, right-packed: " 5 d", "45 d", "456d"
  kLabelBlankZero,  // like kLabel, but the value is blanked when 0: "   h"
  kColon,           // " 9:05" - left blank-padded, right zero-padded
  kColonBlink,      // like kColon; colon follows colonVisible (" 905" when off)
  kColonTenths,     // "59:4" - value plus a single tenths digit
  kDow,             // day-of-week text, right-justified
};

struct PanelSpec {
  Shape shape;
  Field a;  // main / left-of-colon value
  Field b;  // right-of-colon value; kNone otherwise
};

struct FormatSpec {
  const char* label;
  PanelSpec panels[kDisplayPanelCount];
};

using S = Shape;
using F = Field;

// RefreshRate and ColonAnimation are derived from the panel shapes
// (kColonTenths / kColonBlink), so a row's metadata can never drift from what
// it renders. The hhh:mm overflow fallback is resolved semantically in
// resolveCountingOverflow(); no hardcoded indices.
const FormatSpec kCountingFormats[] = {
  {"dd D |  hh:mm |  ss:u", {{S::kLabel, F::kDays},           {S::kColon, F::kHours, F::kMinutes},      {S::kColonTenths, F::kSeconds, F::kTenths}}},
  {"dd D |  hh:mm |    ss", {{S::kLabel, F::kDays},           {S::kColon, F::kHours, F::kMinutes},      {S::kNumber, F::kSeconds}}},
  {"dd D |  hh  H | mm:ss", {{S::kLabel, F::kDays},           {S::kLabelBlankZero, F::kHours},          {S::kColon, F::kMinutes, F::kSeconds}}},
  {"dd D |  hh  H |  mm N", {{S::kLabel, F::kDays},           {S::kLabelBlankZero, F::kHours},          {S::kLabel, F::kMinutes}}},
  {"  dd |  hh:mm |  ss:u", {{S::kNumber, F::kDays},          {S::kColon, F::kHours, F::kMinutes},      {S::kColonTenths, F::kSeconds, F::kTenths}}},
  {"  dd |  hh:mm |    ss", {{S::kNumber, F::kDays},          {S::kColon, F::kHours, F::kMinutes},      {S::kNumber, F::kSeconds}}},
  {"  dd |     hh | mm:ss", {{S::kNumber, F::kDays},          {S::kNumberBlankZero, F::kHours},         {S::kColon, F::kMinutes, F::kSeconds}}},
  {"  dd |     hh |    mm", {{S::kNumber, F::kDays},          {S::kNumberBlankZero, F::kHours},         {S::kNumber, F::kMinutes}}},
  {"hh H |   mm N |  ss:u", {{S::kLabelBlankZero, F::kHours}, {S::kLabel, F::kMinutes},                 {S::kColonTenths, F::kSeconds, F::kTenths}}},
  {"hh H |   mm N |    ss", {{S::kLabelBlankZero, F::kHours}, {S::kLabel, F::kMinutes},                 {S::kNumber, F::kSeconds}}},
  {" hhh |     mm |  ss:u", {{S::kNumber, F::kTotalHours},    {S::kNumber, F::kMinutes},                {S::kColonTenths, F::kSeconds, F::kTenths}}},
  {" hhh |     mm |    ss", {{S::kNumber, F::kTotalHours},    {S::kNumber, F::kMinutes},                {S::kNumber, F::kSeconds}}},
  {"     | hhh:mm |  ss:u", {{S::kBlank},                     {S::kColon, F::kTotalHours, F::kMinutes}, {S::kColonTenths, F::kSeconds, F::kTenths}}},
  {"     | hhh:mm |    ss", {{S::kBlank},                     {S::kColon, F::kTotalHours, F::kMinutes}, {S::kNumber, F::kSeconds}}},
};

const FormatSpec kClockFormats[] = {
  {" DOW  | MM:DD | hh;mm", {{S::kDow, F::kDow},              {S::kColon, F::kMonth, F::kDay},          {S::kColonBlink, F::kHours, F::kMinutes}}},
  {" DOW  |       | hh;mm", {{S::kDow, F::kDow},              {S::kBlank},                              {S::kColonBlink, F::kHours, F::kMinutes}}},
  {"      |   DOW | hh;mm", {{S::kBlank},                     {S::kDow, F::kDow},                       {S::kColonBlink, F::kHours, F::kMinutes}}},
  {" DOW  |    MM |    DD", {{S::kDow, F::kDow},              {S::kNumber, F::kMonth},                  {S::kNumber, F::kDay}}},
  {" DOW  | hh:mm |  ss:u", {{S::kDow, F::kDow},              {S::kColon, F::kHours, F::kMinutes},      {S::kColonTenths, F::kSeconds, F::kTenths}}},
  {" DOW  | hh:mm |    ss", {{S::kDow, F::kDow},              {S::kColon, F::kHours, F::kMinutes},      {S::kNumber, F::kSeconds}}},
  {" DOW  | hh  H | mm:ss", {{S::kDow, F::kDow},              {S::kLabelBlankZero, F::kHours},          {S::kColon, F::kMinutes, F::kSeconds}}},
  {" DOW  | hh  H |  mm N", {{S::kDow, F::kDow},              {S::kLabelBlankZero, F::kHours},          {S::kLabel, F::kMinutes}}},
  {" YYYY | MM:DD | hh;mm", {{S::kNumber, F::kYear},          {S::kColon, F::kMonth, F::kDay},          {S::kColonBlink, F::kHours, F::kMinutes}}},
  {" YYYY |    MM |    DD", {{S::kNumber, F::kYear},          {S::kNumber, F::kMonth},                  {S::kNumber, F::kDay}}},
  {"   MM |    DD | hh;mm", {{S::kNumber, F::kMonth},         {S::kNumber, F::kDay},                    {S::kColonBlink, F::kHours, F::kMinutes}}},
  {"MM:DD | hh:mm |  ss:u", {{S::kColon, F::kMonth, F::kDay}, {S::kColon, F::kHours, F::kMinutes},      {S::kColonTenths, F::kSeconds, F::kTenths}}},
  {"MM:DD | hh:mm |    ss", {{S::kColon, F::kMonth, F::kDay}, {S::kColon, F::kHours, F::kMinutes},      {S::kNumber, F::kSeconds}}},
  {"MM:DD |    hh | mm:ss", {{S::kColon, F::kMonth, F::kDay}, {S::kNumber, F::kHours},                  {S::kColon, F::kMinutes, F::kSeconds}}},
  {"MM:DD |    hh |    mm", {{S::kColon, F::kMonth, F::kDay}, {S::kNumber, F::kHours},                  {S::kNumber, F::kMinutes}}},
  {"   DD | hh:mm |  ss:u", {{S::kNumber, F::kDay},           {S::kColon, F::kHours, F::kMinutes},      {S::kColonTenths, F::kSeconds, F::kTenths}}},
  {"   DD | hh:mm |    ss", {{S::kNumber, F::kDay},           {S::kColon, F::kHours, F::kMinutes},      {S::kNumber, F::kSeconds}}},
  {"   DD |    hh | mm:ss", {{S::kNumber, F::kDay},           {S::kNumber, F::kHours},                  {S::kColon, F::kMinutes, F::kSeconds}}},
  {"   DD |    hh |    mm", {{S::kNumber, F::kDay},           {S::kNumber, F::kHours},                  {S::kNumber, F::kMinutes}}},
};

constexpr uint8_t kCountingFormatCount = sizeof(kCountingFormats) / sizeof(kCountingFormats[0]);
constexpr uint8_t kClockFormatCount = sizeof(kClockFormats) / sizeof(kClockFormats[0]);

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

int fieldValue(Field field, const RenderValues& v) {
  switch (field) {
    case Field::kDays: return v.days;
    case Field::kHours: return v.hours;
    case Field::kTotalHours: return v.totalHours;
    case Field::kMinutes: return v.minutes;
    case Field::kSeconds: return v.seconds;
    case Field::kTenths: return v.tenths;
    case Field::kYear: return v.year;
    case Field::kMonth: return v.month;
    case Field::kDay: return v.day;
    case Field::kDow: return v.dayOfWeek;
    default: return 0;
  }
}

char labelFor(Field field) {
  switch (field) {
    case Field::kDays: return 'd';
    case Field::kHours: return 'h';
    case Field::kMinutes: return 'n';
    default: return ' ';
  }
}

const char* dayOfWeekAbbreviation(int dayOfWeek) {
  static const char* const kNames[] = {
      "Sun", "non", "tu", "uEd", "thu", "Fri", "Sat"};
  return dayOfWeek >= 0 && dayOfWeek < 7 ? kNames[dayOfWeek] : "   ";
}

// Right-packed value + unit letter. The label hugs the value and is dropped
// entirely when the value needs all four digits: " 5 d", "45 d", "456d", "4567".
void formatLabeled(char* out, int value, char label, bool blankIfZero) {
  value = constrain(value, 0, 9999);
  if (blankIfZero && value == 0) {
    snprintf(out, kDisplayFramePanelSize, "   %c", label);
  } else if (value < 10) {
    snprintf(out, kDisplayFramePanelSize, " %d %c", value, label);
  } else if (value < 100) {
    snprintf(out, kDisplayFramePanelSize, "%2d %c", value, label);
  } else if (value < 1000) {
    snprintf(out, kDisplayFramePanelSize, "%3d%c", value, label);
  } else {
    snprintf(out, kDisplayFramePanelSize, "%4d", value);
  }
}

void renderPanel(const PanelSpec& spec, const RenderValues& v, char* out) {
  const int a = fieldValue(spec.a, v);
  const int b = fieldValue(spec.b, v);
  switch (spec.shape) {
    case Shape::kBlank:
      snprintf(out, kDisplayFramePanelSize, "    ");
      break;
    case Shape::kNumber:
      snprintf(out, kDisplayFramePanelSize, "%4d", a);
      break;
    case Shape::kNumberBlankZero:
      if (a == 0) snprintf(out, kDisplayFramePanelSize, "    ");
      else snprintf(out, kDisplayFramePanelSize, "%4d", a);
      break;
    case Shape::kLabel:
      formatLabeled(out, a, labelFor(spec.a), false);
      break;
    case Shape::kLabelBlankZero:
      formatLabeled(out, a, labelFor(spec.a), true);
      break;
    case Shape::kColon:
      snprintf(out, kDisplayFramePanelSize, "%2d:%02d", a, b);
      break;
    case Shape::kColonBlink:
      if (v.colonVisible) snprintf(out, kDisplayFramePanelSize, "%2d:%02d", a, b);
      else snprintf(out, kDisplayFramePanelSize, "%2d%02d", a, b);
      break;
    case Shape::kColonTenths:
      snprintf(out, kDisplayFramePanelSize, "%2d:%d", a, b);
      break;
    case Shape::kDow:
      snprintf(out, kDisplayFramePanelSize, "%4s", dayOfWeekAbbreviation(a));
      break;
  }
}

const FormatSpec& safeFormat(FormatGroup group, uint8_t index) {
  if (group == kFmtGroupClock) {
    return kClockFormats[index < kClockFormatCount ? index : 0];
  }
  return kCountingFormats[index < kCountingFormatCount ? index : 0];
}

bool samePanel(const PanelSpec& a, const PanelSpec& b) {
  return a.shape == b.shape && a.a == b.a && a.b == b.b;
}

bool rendersCombinedTotalHours(const FormatSpec& format) {
  for (const PanelSpec& panel : format.panels) {
    if (panel.shape == Shape::kColon && panel.a == Field::kTotalHours) return true;
  }
  return false;
}

// A combined hhh:mm panel only fits through 99:59. Above that, fall back to
// the split format that shows the same content: total hours and minutes on
// their own panels, with an identical seconds panel.
const FormatSpec& resolveCountingOverflow(const FormatSpec& format,
                                          int totalHours) {
  if (totalHours <= 99 || !rendersCombinedTotalHours(format)) return format;
  for (const FormatSpec& candidate : kCountingFormats) {
    if (candidate.panels[0].shape == Shape::kNumber &&
        candidate.panels[0].a == Field::kTotalHours &&
        candidate.panels[1].shape == Shape::kNumber &&
        candidate.panels[1].a == Field::kMinutes &&
        samePanel(candidate.panels[2], format.panels[2])) {
      return candidate;
    }
  }
  return format;
}

DisplayFrame renderPanels(const FormatSpec& format, const RenderValues& values) {
  DisplayFrame frame;
  for (uint8_t panel = 0; panel < kDisplayPanelCount; ++panel) {
    renderPanel(format.panels[panel], values, frame.panels[panel]);
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
  // The 4-digit day panels saturate at 9999; totalHours keeps the true value.
  if (values.days > 9999) values.days = 9999;
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

DisplayFormatInfo displayFormatInfo(FormatGroup group, uint8_t index) {
  const FormatSpec& format = safeFormat(group, index);
  DisplayFormatInfo info{format.label, RefreshRate::kOneSecond,
                         ColonAnimation::kNone};
  for (const PanelSpec& panel : format.panels) {
    if (panel.shape == Shape::kColonTenths) {
      info.refreshRate = RefreshRate::kOneTenth;
    }
    if (panel.shape == Shape::kColonBlink) {
      info.colonAnimation = ColonAnimation::kBlinking;
    }
  }
  return info;
}

DisplayFrame renderCountingFormat(uint8_t index, long totalSeconds,
                                  uint8_t tenths) {
  const RenderValues values = valuesFromDuration(totalSeconds, tenths);
  const FormatSpec& format = resolveCountingOverflow(
      safeFormat(kFmtGroupCountdown, index), values.totalHours);
  return renderPanels(format, values);
}

DisplayFrame renderClockFormat(uint8_t index, const DateTime& now,
                               bool use12Hour, uint8_t tenths,
                               bool colonVisible) {
  return renderPanels(safeFormat(kFmtGroupClock, index),
                      valuesFromClock(now, use12Hour, tenths, colonVisible));
}
