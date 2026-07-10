#include "format.h"
#include <string.h>

// -- Counting formats (shared by Countdown + Count-Up) ------------------------
// Tokens: dd=days  hh/mm/ss=zero-padded time  u=tenths
// Literals: D=days-label  H=hours-label  N=minutes-label
// Separator: " | " between rows; whitespace inside rows is normalized
// (leading/trailing ignored, multiple internal spaces treated as one).
// hhh (formats 10-13): no days row at all - hours accumulate past 24
// (f.days * 24 + f.hours) instead of wrapping into a separate days count.
// Any format using hhh:mm on a 4-digit row only works through 99:59; above
// 99 hours, rendering auto-falls back to a compatible hhh | mm variant.
static const FormatEntry kCountingEntries[] = {
  {"dd D |  hh:mm |  ss:u", {true,  false}},  // 0:  days D  | hours:minutes | seconds:tenths
  {"dd D |  hh:mm |    ss", {false, false}},  // 1:  days D  | hours:minutes | seconds
  {"dd D |  hh  H | mm:ss", {false, false}},  // 2:  days D  | hours H       | minutes:seconds
  {"dd D |  hh  H |  mm N", {false, false}},  // 3:  days D  | hours H       | minutes N
  {"  dd |  hh:mm |  ss:u", {true,  false}},  // 4:  days    | hours:minutes | seconds:tenths
  {"  dd |  hh:mm |    ss", {false, false}},  // 5:  days    | hours:minutes | seconds
  {"  dd |     hh | mm:ss", {false, false}},  // 6:  days    | hours         | minutes:seconds
  {"  dd |     hh |    mm", {false, false}},  // 7:  days    | hours         | minutes
  {"hh H |   mm N |  ss:u", {true,  false}},  // 8:  hours H | minutes N     | seconds:tenths
  {"hh H |   mm N |    ss", {true,  false}},  // 9:  hours H | minutes N     | seconds
  {" hhh |     mm |  ss:u", {true,  false}},  // 10: hours   | minutes       | seconds:tenths
  {" hhh |     mm |    ss", {false, false}},  // 11: hours   | minutes       | seconds
  {"     | hhh:mm |  ss:u", {true,  false}},  // 12: blank   | hours:minutes | seconds:tenths (auto-fallback when hours > 99)
  {"     | hhh:mm |    ss", {false, false}},  // 13: blank   | hours:minutes | seconds        (auto-fallback when hours > 99)
};

// -- Clock formats -------------------------------------------------------------
// Tokens: YYYY=year  MM=month  DD=day-of-month  DOW=day-of-week  hh/mm/ss=time  u=tenths
// Literals: H=hours-label  N=minutes-label
// A semi-colon in hh;mm marks the colon as blinking.
static const FormatEntry kClockEntries[] = {
  {" DOW  | MM:DD | hh;mm", {false, true}},   //  0: DayOfWeek  | month:day     | hours:minutes  (blink)
  {" DOW  |       | hh;mm", {false, true}},   //  1: DayOfWeek  | blank         | hours:minutes  (blink)
  {"      |   DOW | hh;mm", {false, true}},   //  2: blank      | DayOfWeek     | hours:minutes  (blink)
  {" DOW  |    MM |    DD", {false, false}},  //  3: DayOfWeek  | month         | day
  {" DOW  | hh:mm |  ss:u", {true,  false}},  //  4: DayOfWeek  | hours:minutes | seconds:tenths
  {" DOW  | hh:mm |    ss", {false, false}},  //  5: DayOfWeek  | hours:minutes | seconds
  {" DOW  | hh  H | mm:ss", {false, false}},  //  6: DayOfWeek  | hours h       | minutes:seconds
  {" DOW  | hh  H |  mm N", {false, false}},  //  7: DayOfWeek  | hours h       | minutes n
  {" YYYY | MM:DD | hh;mm", {false, true}},   //  8: year       | month:day     | hours:minutes  (blink)
  {" YYYY |    MM |    DD", {false, false}},  //  9: year       | month         | day
  {"   MM |    DD | hh;mm", {false, true}},   // 10: month      | day           | hours:minutes  (blink)
  {"MM:DD | hh:mm |  ss:u", {true,  false}},  // 11: month:day  | hours:minutes | seconds tenths
  {"MM:DD | hh:mm |    ss", {false, false}},  // 12: month:day  | hours:minutes | seconds
  {"MM:DD |    hh | mm:ss", {false, false}},  // 13: month:day  | hours         | minutes:seconds
  {"MM:DD |    hh |    mm", {false, false}},  // 14: month:day  | hours         | minutes
  {"   DD | hh:mm |  ss:u", {true,  false}},  // 15: day        | hours:minutes | seconds tenths
  {"   DD | hh:mm |    ss", {false, false}},  // 16: day        | hours:minutes | seconds
  {"   DD |    hh | mm:ss", {false, false}},  // 17: day        | hours         | minutes:seconds
  {"   DD |    hh |    mm", {false, false}},  // 18: day        | hours         | minutes
};

namespace {

struct FormatTableView {
  const FormatEntry* entries;
  uint8_t size;
};

bool containsToken(const char* value, const char* token) {
  return value != nullptr && token != nullptr && strstr(value, token) != nullptr;
}

bool isHhhMinuteCombined(const char* format) {
  return containsToken(format, "hhh:mm");
}

bool isHhhMinuteSplit(const char* format) {
  if (!containsToken(format, "hhh")) return false;
  if (!containsToken(format, "mm")) return false;
  return !isHhhMinuteCombined(format);
}

FormatTableView tableForGroup(FormatGroup group) {
  switch (group) {
    case kFmtGroupCountdown:
    case kFmtGroupCountUp:
      // Keep countdown/countup format definitions in one place so the two
      // modes cannot silently drift out of sync.
      return {kCountingEntries,
              static_cast<uint8_t>(sizeof(kCountingEntries) / sizeof(kCountingEntries[0]))};
    case kFmtGroupClock:
      return {kClockEntries,
              static_cast<uint8_t>(sizeof(kClockEntries) / sizeof(kClockEntries[0]))};
    default:
      return {nullptr, 0};
  }
}

}  // namespace

uint8_t formatCount(FormatGroup group) {
  return tableForGroup(group).size;
}

const char* getFormat(FormatGroup group, uint8_t index) {
  const FormatTableView table = tableForGroup(group);
  return (table.entries != nullptr && index < table.size) ? table.entries[index].format : nullptr;
}

const FormatMetadata* getFormatMeta(FormatGroup group, uint8_t index) {
  const FormatTableView table = tableForGroup(group);
  return (table.entries != nullptr && index < table.size) ? &table.entries[index].meta : nullptr;
}

uint8_t resolveCountingOverflowIndex(uint8_t index, int totalHours) {
  if (totalHours <= 99) return index;

  const FormatTableView table = tableForGroup(kFmtGroupCountdown);
  if (table.entries == nullptr || index >= table.size) return index;

  const FormatEntry& source = table.entries[index];
  if (!isHhhMinuteCombined(source.format)) return index;

  for (uint8_t candidate = 0; candidate < table.size; ++candidate) {
    if (candidate == index) continue;

    const FormatEntry& entry = table.entries[candidate];
    if (entry.meta.hasTenths != source.meta.hasTenths) continue;
    if (!isHhhMinuteSplit(entry.format)) continue;
    return candidate;
  }

  return index;
}

