#include "format.h"
#include <string.h>

// ── Countdown formats ─────────────────────────────────────────────────────────
// Tokens: dd=days  hh/mm/ss=zero-padded time  u=tenths
// Literals: D=days-label  H=hours-label  N=minutes-label
// Separator: " | " between rows; spaces within a row are display padding.
static const char* const kCountdownFormats[] = {
  "dd D | hh:mm |  ss.u",   // days  D | hours:minutes | seconds.tenths
  "dd D | hh:mm |    ss",   // days  D | hours:minutes | seconds
  "dd D | hh  H | mm:ss",   // days  D | hours  H      | minutes:seconds
  "dd D | hh  H |  mm N",   // days  D | hours  H      | minutes  N
  "  dd | hh:mm |  ss.u",   // days    | hours:minutes | seconds.tenths
  "  dd | hh:mm |    ss",   // days    | hours:minutes | seconds
  "  dd |    hh | mm:ss",   // days    | hours         | minutes:seconds
  "  dd |    hh |    mm",   // days    | hours         | minutes
};

// ── Count-Up formats ──────────────────────────────────────────────────────────
static const char* const kCountupFormats[] = {
  "dd D | hh:mm |  ss.u",
  "dd D | hh:mm |    ss",
  "dd D | hh  H | mm:ss",
  "dd D | hh  H |  mm N",
  "  dd | hh:mm |  ss.u",
  "  dd | hh:mm |    ss",
  "  dd |    hh | mm:ss",
  "  dd |    hh |    mm",
};

// ── Clock formats ─────────────────────────────────────────────────────────────
// Tokens: YYYY=year  MM=month  DD=day-of-month  hh/mm/ss=time  u=tenths
// The two identical "YYYY | MM:DD | hh:mm" entries differ only in colon blink:
//   semi-colon means replace with colon and blink the colon every 500ms and show a colon
static const char* const kClockFormats[] = {
  " YYYY | MM:DD | hh;mm",   // year | month:day | hours:minutes  (colon blinks)
  " YYYY | MM:DD | hh:mm",   // year | month:day | hours:minutes  (colon static)
  " YYYY |    MM |    DD",   // year | month      | day
  "   MM |    DD | hh;mm",   // month | day       | hours:minutes  (colon blinks)
  "   MM |    DD | hh:mm",   // month | day       | hours:minutes  (colon static)
  "MM:DD | hh:mm | ss  u",   // month:day | hours:minutes | seconds tenths
  "MM:DD | hh:mm |    ss",   // month:day | hours:minutes | seconds
  "MM:DD |    hh | mm:ss",   // month:day | hours         | minutes:seconds
  "MM:DD |    hh |    mm",   // month:day | hours         | minutes
  "   DD | hh:mm | ss  u",   // day       | hours:minutes | seconds tenths
  "   DD | hh:mm |    ss",   // day       | hours:minutes | seconds
  "   DD |    hh | mm:ss",   // day       | hours         | minutes:seconds
  "   DD |    hh |    mm",   // day       | hours         | minutes
};

// ── Justification modes ───────────────────────────────────────────────────────
// Controls how blank-padded numeric tokens (d, h, m, s) are aligned.
static const char* const kJustificationModes[] = {
  " Left justified",   // leading space keeps width equal to "Right justified"
  "Right justified",
};

// ── Master lookup table ───────────────────────────────────────────────────────
const char* const* const kFormatGroups[kFmtGroupCount] = {
  kCountdownFormats,
  kCountupFormats,
  kClockFormats,
  kJustificationModes,
};

const uint8_t kFormatGroupSizes[kFmtGroupCount] = {
  (uint8_t)(sizeof(kCountdownFormats)    / sizeof(kCountdownFormats[0])),
  (uint8_t)(sizeof(kCountupFormats)      / sizeof(kCountupFormats[0])),
  (uint8_t)(sizeof(kClockFormats)        / sizeof(kClockFormats[0])),
  (uint8_t)(sizeof(kJustificationModes)  / sizeof(kJustificationModes[0])),
};

