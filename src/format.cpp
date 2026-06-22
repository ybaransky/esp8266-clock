#include "format.h"
#include <string.h>

// -- Countdown formats ---------------------------------------------------------
// Tokens: dd=days  hh/mm/ss=zero-padded time  u=tenths
// Literals: d=days-label  H=hours-label  N=minutes-label
// Separator: " | " between rows; spaces within a row are display padding.
static const char* const kCountdownFormats[] = {
  "dd d | hh:mm |  ss.u",   // days  d | hours:minutes | seconds.tenths
  "dd d | hh:mm |    ss",   // days  d | hours:minutes | seconds
  "dd d | hh  H | mm:ss",   // days  d | hours  H      | minutes:seconds
  "dd d | hh  H |  mm N",   // days  d | hours  H      | minutes  N
  "  dd | hh:mm |  ss.u",   // days    | hours:minutes | seconds.tenths
  "  dd | hh:mm |    ss",   // days    | hours:minutes | seconds
  "  dd |    hh | mm:ss",   // days    | hours         | minutes:seconds
  "  dd |    hh |    mm",   // days    | hours         | minutes
};

static const FormatMetadata kCountdownMeta[] = {
  {true,  false},  // 0: ss.u
  {false, false},  // 1: ss
  {false, false},  // 2: mm:ss
  {false, false},  // 3: mm N
  {true,  false},  // 4: ss.u
  {false, false},  // 5: ss
  {false, false},  // 6: mm:ss
  {false, false},  // 7: mm
};

// -- Count-Up formats ----------------------------------------------------------
static const char* const kCountupFormats[] = {
  "dd d | hh:mm |  ss.u",
  "dd d | hh:mm |    ss",
  "dd d | hh  H | mm:ss",
  "dd d | hh  H |  mm N",
  "  dd | hh:mm |  ss.u",
  "  dd | hh:mm |    ss",
  "  dd |    hh | mm:ss",
  "  dd |    hh |    mm",
};

static const FormatMetadata kCountupMeta[] = {
  {true,  false},  // 0: ss.u
  {false, false},  // 1: ss
  {false, false},  // 2: mm:ss
  {false, false},  // 3: mm N
  {true,  false},  // 4: ss.u
  {false, false},  // 5: ss
  {false, false},  // 6: mm:ss
  {false, false},  // 7: mm
};

// -- Clock formats -------------------------------------------------------------
// Tokens: YYYY=year  MM=month  DD=day-of-month  DOFW=day-of-week  hh/mm/ss=time  u=tenths
// Literals: H=hours-label rendered as h  N=minutes-label rendered as n
// A semi-colon marks the hh:mm separator as blinking.
static const char* const kClockFormats[] = {
  " DOFW | MM:DD | hh;mm",   // DayOfWeek | month:day | hours:minutes  (colon blinks)
  " DOFW |    MM |    DD",   // DayOfWeek | month     | day
  " DOFW | hh:mm |  ss:u",   // DayOfWeek | hours:minutes | seconds:tenths
  " DOFW | hh:mm |    ss",   // DayOfWeek | hours:minutes | seconds
  " DOFW | hh  H | mm:ss",   // DayOfWeek | hours  h  | minutes:seconds
  " DOFW | hh  H |  mm N",   // DayOfWeek | hours  h  | minutes n
  " YYYY | MM:DD | hh;mm",   // year | month:day | hours:minutes  (colon blinks)
  " YYYY |    MM |    DD",   // year | month      | day
  "   MM |    DD | hh;mm",   // month | day       | hours:minutes  (colon blinks)
  "MM:DD | hh:mm | ss  u",   // month:day | hours:minutes | seconds tenths
  "MM:DD | hh:mm |    ss",   // month:day | hours:minutes | seconds
  "MM:DD |    hh | mm:ss",   // month:day | hours         | minutes:seconds
  "MM:DD |    hh |    mm",   // month:day | hours         | minutes
  "   DD | hh:mm | ss  u",   // day       | hours:minutes | seconds tenths
  "   DD | hh:mm |    ss",   // day       | hours:minutes | seconds
  "   DD |    hh | mm:ss",   // day       | hours         | minutes:seconds
  "   DD |    hh |    mm",   // day       | hours         | minutes
};

static const FormatMetadata kClockMeta[] = {
  {false, true},   // 0:  DOFW | MM:DD | hh;mm    (blink colon)
  {false, false},  // 1:  DOFW | MM    | DD
  {true,  false},  // 2:  DOFW | hh:mm | ss:u
  {false, false},  // 3:  DOFW | hh:mm | ss
  {false, false},  // 4:  DOFW | hh h  | mm:ss
  {false, false},  // 5:  DOFW | hh h  | mm n
  {false, true},   // 6:  YYYY | MM:DD | hh;mm    (blink colon)
  {false, false},  // 7:  YYYY | MM    | DD
  {false, true},   // 8:  MM   | DD    | hh;mm    (blink colon)
  {true,  false},  // 9:  MM:DD | hh:mm | ss u
  {false, false},  // 10: MM:DD | hh:mm | ss
  {false, false},  // 11: MM:DD | hh    | mm:ss
  {false, false},  // 12: MM:DD | hh    | mm
  {true,  false},  // 13: DD   | hh:mm | ss u
  {false, false},  // 14: DD   | hh:mm | ss
  {false, false},  // 15: DD   | hh    | mm:ss
  {false, false},  // 16: DD   | hh    | mm
};

// -- Master lookup tables ------------------------------------------------------
const char* const* const kFormatGroups[kFmtGroupCount] = {
  kCountdownFormats,
  kCountupFormats,
  kClockFormats,
};

const FormatMetadata* const kFormatGroupMeta[kFmtGroupCount] = {
  kCountdownMeta,
  kCountupMeta,
  kClockMeta,
};

const uint8_t kFormatGroupSizes[kFmtGroupCount] = {
  (uint8_t)(sizeof(kCountdownFormats)    / sizeof(kCountdownFormats[0])),
  (uint8_t)(sizeof(kCountupFormats)      / sizeof(kCountupFormats[0])),
  (uint8_t)(sizeof(kClockFormats)        / sizeof(kClockFormats[0])),
};

