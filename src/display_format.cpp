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
  kLabel,           // value + unit letter, right-packed: " 5 d", "45 d", "456d"
  kLabelBlankZero,  // like kLabel, but the value is blanked when 0: "   h"
  kColon,           // " 9:05" - left blank-padded, right zero-padded
  kColonBlink,      // like kColon; colon follows colonVisible (" 905" when off)
  kColonTenths,     // "59:4" - value plus a single tenths digit
  kDow,             // day-of-week text, right-justified
};

// Identifies the layout and source fields used to render one physical panel.
struct PanelSpec {
  Shape shape = Shape::kBlank;  // Panel layout renderer.
  Field a = Field::kNone;  // Main or left-of-colon value.
  Field b = Field::kNone;  // Right-of-colon value; kNone otherwise.
};

// The render specification for one format. The matching key and label live in
// the parallel PROGMEM tables below, indexed identically - keeping the strings
// out of this struct is what keeps them out of DRAM, since a `const char*`
// member would only move the pointer to flash, not the text.
struct FormatSpec {
  PanelSpec panels[kDisplayPanelCount];  // Direct render specification per panel.
};

using S = Shape;
using F = Field;

// panel[0] is the leftmost panel, panel[2] is the rightmost panel. The
// label is a human-readable representation of the format, but it is not
// machine-parsed. The panel shapes are the only source of truth for rendering.
// RefreshRate and ColonAnimation are derived from the panel shapes
// (kColonTenths / kColonBlink), so a row's metadata can never drift from what
// it renders. The hhh:mm overflow fallback is resolved semantically in
// resolveCountingOverflow(); no hardcoded indices.
// Parallel to kCountingFormats, one row per format. PROGMEM: the key and label
// strings together are ~1.2KB across both tables - flash we have, DRAM we
// do not. Read with the accessors at the bottom of this file.
const char kCountingKeys[][kFormatKeyLength] PROGMEM = {
    "ddl-hhmm-ssu",
    "ddl-hhmm-ss",
    "ddl-hhl-mmss",
    "ddl-hhl-mml",
    "dd-hhmm-ssu",
    "dd-hhmm-ss",
    "dd-hh-mmss",
    "dd-hh-mm",
    "hhl-mml-ssu",
    "hhl-mml-ss",
    "thhl-mml-ssu",
    "thhl-mml-ss",
    "thh-mm-ssu",
    "thh-mm-ss",
    "thhmm-ssu",
    "thhmm-ss",
};

const char kCountingLabels[][kFormatLabelLength] PROGMEM = {
    " dd D |  hh:mm |  ss:u",
    " dd D |  hh:mm |    ss",
    " dd D |  hh  H | mm:ss",
    " dd D |  hh  H |  mm N",
    "   dd |  hh:mm |  ss:u",
    "   dd |  hh:mm |    ss",
    "   dd |     hh | mm:ss",
    "   dd |     hh |    mm",
    " hh H |   mm N |  ss:u",
    " hh H |   mm N |    ss",
    "hhh H |   mm N |  ss:u",
    "hhh H |   mm N |    ss",
    "  hhh |     mm |  ss:u",
    "  hhh |     mm |    ss",
    "      | hhh:mm |  ss:u",
    "      | hhh:mm |    ss",
};

const FormatSpec kCountingFormats[] = {
    {{{S::kLabel, F::kDays},        {S::kColon, F::kHours, F::kMinutes},      {S::kColonTenths, F::kSeconds, F::kTenths}}},
    {{{S::kLabel, F::kDays},        {S::kColon, F::kHours, F::kMinutes},      {S::kNumber, F::kSeconds}}},
    {{{S::kLabel, F::kDays},        {S::kLabel, F::kHours},                   {S::kColon, F::kMinutes, F::kSeconds}}},
    {{{S::kLabel, F::kDays},        {S::kLabel, F::kHours},                   {S::kLabel, F::kMinutes}}},
    {{{S::kNumber, F::kDays},       {S::kColon, F::kHours, F::kMinutes},      {S::kColonTenths, F::kSeconds, F::kTenths}}},
    {{{S::kNumber, F::kDays},       {S::kColon, F::kHours, F::kMinutes},      {S::kNumber, F::kSeconds}}},
    {{{S::kNumber, F::kDays},       {S::kNumber, F::kHours},                  {S::kColon, F::kMinutes, F::kSeconds}}},
    {{{S::kNumber, F::kDays},       {S::kNumber, F::kHours},                  {S::kNumber, F::kMinutes}}},
    {{{S::kLabel, F::kHours},       {S::kLabel, F::kMinutes},                 {S::kColonTenths, F::kSeconds, F::kTenths}}},
    {{{S::kLabel, F::kHours},       {S::kLabel, F::kMinutes},                 {S::kNumber, F::kSeconds}}},
    {{{S::kLabel, F::kTotalHours},  {S::kLabel, F::kMinutes},                 {S::kColonTenths, F::kSeconds, F::kTenths}}},
    {{{S::kLabel, F::kTotalHours},  {S::kLabel, F::kMinutes},                 {S::kNumber, F::kSeconds}}},
    {{{S::kNumber, F::kTotalHours}, {S::kNumber, F::kMinutes},                {S::kColonTenths, F::kSeconds, F::kTenths}}},
    {{{S::kNumber, F::kTotalHours}, {S::kNumber, F::kMinutes},                {S::kNumber, F::kSeconds}}},
    {{{S::kBlank},                  {S::kColon, F::kTotalHours, F::kMinutes}, {S::kColonTenths, F::kSeconds, F::kTenths}}},
    {{{S::kBlank},                  {S::kColon, F::kTotalHours, F::kMinutes}, {S::kNumber, F::kSeconds}}},
};

// Parallel to kClockFormats, one row per format. PROGMEM: the key and label
// strings together are ~1.2KB across both tables - flash we have, DRAM we
// do not. Read with the accessors at the bottom of this file.
const char kClockKeys[][kFormatKeyLength] PROGMEM = {
    "dow-mmdd-hhmm",
    "dow-blank-hhmm",
    "blank-dow-hhmm",
    "dow-mm-dd",
    "dow-hhmm-ssu",
    "dow-hhmm-ss",
    "dow-hhl-mmss",
    "dow-hhl-mml",
    "yyyy-mmdd-hhmm",
    "yyyy-mm-dd",
    "mm-dd-hhmm",
    "mmdd-hhmm-ssu",
    "mmdd-hhmm-ss",
    "mmdd-hh-mmss",
    "mmdd-hh-mm",
    "dd-hhmm-ssu",
    "dd-hhmm-ss",
    "dd-hh-mmss",
    "dd-hh-mm",
};

const char kClockLabels[][kFormatLabelLength] PROGMEM = {
    " DOW  | MM:DD | hh;mm",
    " DOW  |       | hh;mm",
    "      |   DOW | hh;mm",
    " DOW  |    MM |    DD",
    " DOW  | hh:mm |  ss:u",
    " DOW  | hh:mm |    ss",
    " DOW  | hh  H | mm:ss",
    " DOW  | hh  H |  mm N",
    " YYYY | MM:DD | hh;mm",
    " YYYY |    MM |    DD",
    "   MM |    DD | hh;mm",
    "MM:DD | hh:mm |  ss:u",
    "MM:DD | hh:mm |    ss",
    "MM:DD |    hh | mm:ss",
    "MM:DD |    hh |    mm",
    "   DD | hh:mm |  ss:u",
    "   DD | hh:mm |    ss",
    "   DD |    hh | mm:ss",
    "   DD |    hh |    mm",
};

const FormatSpec kClockFormats[] = {
    {{{S::kDow, F::kDow},              {S::kColon, F::kMonth, F::kDay},          {S::kColonBlink, F::kHours, F::kMinutes}}},
    {{{S::kDow, F::kDow},              {S::kBlank},                              {S::kColonBlink, F::kHours, F::kMinutes}}},
    {{{S::kBlank},                     {S::kDow, F::kDow},                       {S::kColonBlink, F::kHours, F::kMinutes}}},
    {{{S::kDow, F::kDow},              {S::kNumber, F::kMonth},                  {S::kNumber, F::kDay}}},
    {{{S::kDow, F::kDow},              {S::kColon, F::kHours, F::kMinutes},      {S::kColonTenths, F::kSeconds, F::kTenths}}},
    {{{S::kDow, F::kDow},              {S::kColon, F::kHours, F::kMinutes},      {S::kNumber, F::kSeconds}}},
    {{{S::kDow, F::kDow},              {S::kLabelBlankZero, F::kHours},          {S::kColon, F::kMinutes, F::kSeconds}}},
    {{{S::kDow, F::kDow},              {S::kLabelBlankZero, F::kHours},          {S::kLabel, F::kMinutes}}},
    {{{S::kNumber, F::kYear},          {S::kColon, F::kMonth, F::kDay},          {S::kColonBlink, F::kHours, F::kMinutes}}},
    {{{S::kNumber, F::kYear},          {S::kNumber, F::kMonth},                  {S::kNumber, F::kDay}}},
    {{{S::kNumber, F::kMonth},         {S::kNumber, F::kDay},                    {S::kColonBlink, F::kHours, F::kMinutes}}},
    {{{S::kColon, F::kMonth, F::kDay}, {S::kColon, F::kHours, F::kMinutes},      {S::kColonTenths, F::kSeconds, F::kTenths}}},
    {{{S::kColon, F::kMonth, F::kDay}, {S::kColon, F::kHours, F::kMinutes},      {S::kNumber, F::kSeconds}}},
    {{{S::kColon, F::kMonth, F::kDay}, {S::kNumber, F::kHours},                  {S::kColon, F::kMinutes, F::kSeconds}}},
    {{{S::kColon, F::kMonth, F::kDay}, {S::kNumber, F::kHours},                  {S::kNumber, F::kMinutes}}},
    {{{S::kNumber, F::kDay},           {S::kColon, F::kHours, F::kMinutes},      {S::kColonTenths, F::kSeconds, F::kTenths}}},
    {{{S::kNumber, F::kDay},           {S::kColon, F::kHours, F::kMinutes},      {S::kNumber, F::kSeconds}}},
    {{{S::kNumber, F::kDay},           {S::kNumber, F::kHours},                  {S::kColon, F::kMinutes, F::kSeconds}}},
    {{{S::kNumber, F::kDay},           {S::kNumber, F::kHours},                  {S::kNumber, F::kMinutes}}},
};

constexpr uint8_t kCountingFormatCount = sizeof(kCountingFormats) / sizeof(kCountingFormats[0]);
constexpr uint8_t kClockFormatCount = sizeof(kClockFormats) / sizeof(kClockFormats[0]);

static_assert(sizeof(kCountingKeys) / kFormatKeyLength == kCountingFormatCount,
              "counting key table must have one row per format");
static_assert(sizeof(kCountingLabels) / kFormatLabelLength == kCountingFormatCount,
              "counting label table must have one row per format");
static_assert(sizeof(kClockKeys) / kFormatKeyLength == kClockFormatCount,
              "clock key table must have one row per format");
static_assert(sizeof(kClockLabels) / kFormatLabelLength == kClockFormatCount,
              "clock label table must have one row per format");

// The PROGMEM string row for one format, or the group's first row when the
// index is out of range - matching how safeFormat() falls back.
const char* keyRow(FormatGroup group, uint8_t index) {
  if (group == kFmtGroupClock) {
    return kClockKeys[index < kClockFormatCount ? index : 0];
  }
  return kCountingKeys[index < kCountingFormatCount ? index : 0];
}

const char* labelRow(FormatGroup group, uint8_t index) {
  if (group == kFmtGroupClock) {
    return kClockLabels[index < kClockFormatCount ? index : 0];
  }
  return kCountingLabels[index < kCountingFormatCount ? index : 0];
}

// Collects normalized renderer inputs so panel logic can read fields uniformly.
struct RenderValues {
  int year = 0;  // Four-digit calendar year.
  int month = 0;  // Calendar month from 1 through 12.
  int day = 0;  // Calendar day of month.
  int dayOfWeek = 0;  // RTClib weekday index from Sunday.
  int days = 0;  // Whole elapsed days.
  int hours = 0;  // Clock hour or within-day elapsed hours.
  int totalHours = 0;  // Unbounded elapsed hours.
  int minutes = 0;  // Minute component.
  int seconds = 0;  // Second component.
  int tenths = 0;  // Tenths-of-a-second digit.
  bool colonVisible = true;  // Current phase for blinking-colon formats.
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
    case Field::kTotalHours: return 'h';
    case Field::kMinutes: return 'n';
    default: return ' ';
  }
}

const char* dayOfWeekAbbreviation(int dayOfWeek) {
  static const char* const kNames[] = {
      "Sun", "NNon", "tu", "UUEd", "thu", "Fri", "Sat"};
  return dayOfWeek >= 0 && dayOfWeek < 7 ? kNames[dayOfWeek] : "   ";
}

// Right-packed value + unit letter. The label is dropped entirely once the
// value reaches three digits: " 5 d", "45 d", " 456", "4567".
void formatLabeled(char* out, int value, char label, bool blankIfZero) {
  value = constrain(value, 0, 9999);
  if (blankIfZero && (value == 0)) {
    snprintf(out, kDisplayFramePanelSize, "   %c", label);
  } else if (value < 10) {
    snprintf(out, kDisplayFramePanelSize, " %d %c", value, label);
  } else if (value < 100) {
    snprintf(out, kDisplayFramePanelSize, "%2d %c", value, label);
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

// The table backing a group, with its length. Countdown and CountUp share the
// counting table, which is what keeps the two modes from drifting apart.
const FormatSpec* formatTable(FormatGroup group, uint8_t& count) {
  if (group == kFmtGroupClock) {
    count = kClockFormatCount;
    return kClockFormats;
  }
  count = kCountingFormatCount;
  return kCountingFormats;
}

const FormatSpec& safeFormat(FormatGroup group, uint8_t index) {
  uint8_t count = 0;
  const FormatSpec* table = formatTable(group, count);
  return table[index < count ? index : 0];
}

bool samePanel(const PanelSpec& a, const PanelSpec& b) {
  return a.shape == b.shape && a.a == b.a && a.b == b.b;
}

bool rendersCombinedTotalHours(const FormatSpec& format) {
  for (const PanelSpec& panel : format.panels) {
    if ((panel.shape == Shape::kColon) && (panel.a == Field::kTotalHours)) return true;
  }
  return false;
}

// A combined hhh:mm panel only fits through 99:59. Above that, fall back to
// the split format that shows the same content: total hours and minutes on
// their own panels, with an identical seconds panel.
const FormatSpec& resolveCountingOverflow(const FormatSpec& format,
                                          int totalHours) {
  if ((totalHours <= 99) || !rendersCombinedTotalHours(format)) return format;
  for (const FormatSpec& candidate : kCountingFormats) {
    if ((candidate.panels[0].shape == Shape::kNumber) &&
        (candidate.panels[0].a == Field::kTotalHours) &&
        (candidate.panels[1].shape == Shape::kNumber) &&
        (candidate.panels[1].a == Field::kMinutes) &&
        samePanel(candidate.panels[2], format.panels[2])) {
      return candidate;
    }
  }
  return format;
}

// True when every value field the panel reads is zero, so the panel carries
// no information during a countdown/countup.
bool panelValueIsZero(const PanelSpec& spec, const RenderValues& v) {
  if (spec.shape == Shape::kBlank) return true;
  return (fieldValue(spec.a, v) == 0) && (fieldValue(spec.b, v) == 0);
}

// Counting formats hide leading zero panels: panel 0 blanks when zero, and
// panel 1 blanks only when it is zero and panel 0 is already blank. The last
// panel always renders so the most active unit stays visible. Once a panel is
// visible, everything to its right shows real values, zeros included.
FormatSpec suppressLeadingZeroPanels(FormatSpec format,
                                     const RenderValues& v) {
  if (!panelValueIsZero(format.panels[0], v)) return format;
  format.panels[0] = {S::kBlank, F::kNone, F::kNone};
  if (panelValueIsZero(format.panels[1], v)) {
    format.panels[1] = {S::kBlank, F::kNone, F::kNone};
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

void displayFormatKey(FormatGroup group, uint8_t index, char* out,
                      size_t outSize) {
  if ((out == nullptr) || (outSize == 0)) return;
  strncpy_P(out, keyRow(group, index), outSize);
  out[outSize - 1] = '\0';
}

void displayFormatLabel(FormatGroup group, uint8_t index, char* out,
                        size_t outSize) {
  if ((out == nullptr) || (outSize == 0)) return;
  strncpy_P(out, labelRow(group, index), outSize);
  out[outSize - 1] = '\0';
}

bool displayFormatIndexForKey(FormatGroup group, const char* key,
                              uint8_t* index) {
  if ((key == nullptr) || (key[0] == '\0') || (index == nullptr)) return false;
  uint8_t count = 0;
  formatTable(group, count);
  for (uint8_t i = 0; i < count; ++i) {
    // Compares the caller's RAM string against the flash row in place, so the
    // scan needs no scratch buffer.
    if (strncmp_P(key, keyRow(group, i), kFormatKeyLength) == 0) {
      *index = i;
      return true;
    }
  }
  return false;
}

DisplayFormatInfo displayFormatInfo(FormatGroup group, uint8_t index) {
  const FormatSpec& format = safeFormat(group, index);
  DisplayFormatInfo info{RefreshRate::kOneSecond, ColonAnimation::kNone};
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
  const FormatSpec format = suppressLeadingZeroPanels(
      resolveCountingOverflow(safeFormat(kFmtGroupCountdown, index),
                              values.totalHours),
      values);
  return renderPanels(format, values);
}

DisplayFrame renderClockFormat(uint8_t index, const DateTime& now,
                               bool use12Hour, uint8_t tenths,
                               bool colonVisible) {
  return renderPanels(safeFormat(kFmtGroupClock, index),
                      valuesFromClock(now, use12Hour, tenths, colonVisible));
}
