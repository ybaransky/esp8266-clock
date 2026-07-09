#include "format.h"
#include <string.h>

// -- Countdown formats ---------------------------------------------------------
// Tokens: dd=days  hh/mm/ss=zero-padded time  u=tenths
// Literals: d=days-label  H=hours-label  N=minutes-label
// Separator: " | " between rows; spaces within a row are display padding.
// hhh (formats 8-9): no days row at all - hours accumulate past 24
// (f.days * 24 + f.hours) instead of wrapping into a separate days count.
static const FormatEntry kCountdownEntries[] = {
  {"dd d | hh:mm |  ss.u", {true,  false}},  // 0: days d | hours:minutes | seconds.tenths
  {"dd d | hh:mm |    ss", {false, false}},  // 1: days d | hours:minutes | seconds
  {"dd d | hh  H | mm:ss", {false, false}},  // 2: days d | hours H       | minutes:seconds
  {"dd d | hh  H |  mm N", {false, false}},  // 3: days d | hours H       | minutes N
  {"  dd | hh:mm |  ss.u", {true,  false}},  // 4: days   | hours:minutes | seconds.tenths
  {"  dd | hh:mm |    ss", {false, false}},  // 5: days   | hours:minutes | seconds
  {"  dd |    hh | mm:ss", {false, false}},  // 6: days   | hours         | minutes:seconds
  {"  dd |    hh |    mm", {false, false}},  // 7: days   | hours         | minutes
  {" hhh |   mm |  ss.u", {true,  false}},  // 8: hours (no days) | minutes | seconds.tenths
  {" hhh |   mm |    ss", {false, false}},  // 9: hours (no days) | minutes | seconds
};

// -- Count-Up formats ----------------------------------------------------------
static const FormatEntry kCountupEntries[] = {
  {"dd d | hh:mm |  ss.u", {true,  false}},  // 0: days d | hours:minutes | seconds.tenths
  {"dd d | hh:mm |    ss", {false, false}},  // 1: days d | hours:minutes | seconds
  {"dd d | hh  H | mm:ss", {false, false}},  // 2: days d | hours H       | minutes:seconds
  {"dd d | hh  H |  mm N", {false, false}},  // 3: days d | hours H       | minutes N
  {"  dd | hh:mm |  ss.u", {true,  false}},  // 4: days   | hours:minutes | seconds.tenths
  {"  dd | hh:mm |    ss", {false, false}},  // 5: days   | hours:minutes | seconds
  {"  dd |    hh | mm:ss", {false, false}},  // 6: days   | hours         | minutes:seconds
  {"  dd |    hh |    mm", {false, false}},  // 7: days   | hours         | minutes
  {" hhh |   mm |  ss.u", {true,  false}},  // 8: hours (no days) | minutes | seconds.tenths
  {" hhh |   mm |    ss", {false, false}},  // 9: hours (no days) | minutes | seconds
};

// -- Clock formats -------------------------------------------------------------
// Tokens: YYYY=year  MM=month  DD=day-of-month  DOFW=day-of-week  hh/mm/ss=time  u=tenths
// Literals: H=hours-label rendered as h  N=minutes-label rendered as n
// A semi-colon in hh;mm marks the colon as blinking.
static const FormatEntry kClockEntries[] = {
  {" DOFW | MM:DD | hh;mm", {false, true}},   //  0: DayOfWeek | month:day     | hours:minutes  (blink)
  {" DOFW |       | hh;mm", {false, true}},   //  1: DayOfWeek | blank         | hours:minutes  (blink)
  {" DOFW |    MM |    DD", {false, false}},  //  2: DayOfWeek | month         | day
  {" DOFW | hh:mm |  ss:u", {true,  false}},  //  3: DayOfWeek | hours:minutes | seconds:tenths
  {" DOFW | hh:mm |    ss", {false, false}},  //  4: DayOfWeek | hours:minutes | seconds
  {" DOFW | hh  H | mm:ss", {false, false}},  //  5: DayOfWeek | hours h       | minutes:seconds
  {" DOFW | hh  H |  mm N", {false, false}},  //  6: DayOfWeek | hours h       | minutes n
  {" YYYY | MM:DD | hh;mm", {false, true}},   //  7: year       | month:day     | hours:minutes  (blink)
  {" YYYY |    MM |    DD", {false, false}},  //  8: year       | month         | day
  {"   MM |    DD | hh;mm", {false, true}},   //  9: month      | day           | hours:minutes  (blink)
  {"MM:DD | hh:mm | ss  u", {true,  false}},  // 10: month:day  | hours:minutes | seconds tenths
  {"MM:DD | hh:mm |    ss", {false, false}},  // 11: month:day  | hours:minutes | seconds
  {"MM:DD |    hh | mm:ss", {false, false}},  // 12: month:day  | hours         | minutes:seconds
  {"MM:DD |    hh |    mm", {false, false}},  // 13: month:day  | hours         | minutes
  {"   DD | hh:mm | ss  u", {true,  false}},  // 14: day        | hours:minutes | seconds tenths
  {"   DD | hh:mm |    ss", {false, false}},  // 15: day        | hours:minutes | seconds
  {"   DD |    hh | mm:ss", {false, false}},  // 16: day        | hours         | minutes:seconds
  {"   DD |    hh |    mm", {false, false}},  // 17: day        | hours         | minutes
};

// -- Master lookup tables ------------------------------------------------------
const FormatEntry* const kFormatEntries[kFmtGroupCount] = {
  kCountdownEntries,
  kCountupEntries,
  kClockEntries,
};

const uint8_t kFormatGroupSizes[kFmtGroupCount] = {
  (uint8_t)(sizeof(kCountdownEntries) / sizeof(kCountdownEntries[0])),
  (uint8_t)(sizeof(kCountupEntries)   / sizeof(kCountupEntries[0])),
  (uint8_t)(sizeof(kClockEntries)     / sizeof(kClockEntries[0])),
};

