#pragma once

#include <stddef.h>
#include <stdint.h>

// POSIX TZ rules ("EST5EDT,M3.2.0,M11.1.0") and conversion between UTC and
// local wall-clock seconds. Pure: no Arduino, no libc timezone state, so the
// host tests exercise exactly what the firmware runs.
//
// Supported grammar - everything the IANA-derived zone table uses:
//   std offset [dst [offset] ,start[/time],end[/time]]
// where a name is 3+ letters or <...>, an offset is [+-]hh[:mm[:ss]] (POSIX
// sign: positive means west of Greenwich), and start/end use the Mm.w.d form.
// The Jn and n day forms are rejected rather than half-supported.

// Longest rule string in the zone table is 45 characters; this includes NUL.
static constexpr size_t kTimezoneRuleLength = 48;

// Longest zone abbreviation accepted (e.g. "+1345", "CHADT"), including NUL.
static constexpr size_t kTimezoneNameLength = 8;

// One DST transition: the given weekday of the given week of a month, at a
// local wall-clock time that may run past 24:00 (Jerusalem uses /26).
struct TransitionRule {
  uint8_t month = 0;        // 1-12.
  uint8_t week = 0;         // 1-5; 5 means the last such weekday of the month.
  uint8_t weekday = 0;      // 0 = Sunday.
  int32_t timeSeconds = 0;  // Local time of day the change happens, in seconds.
};

// A parsed timezone: standard offset, and optionally a daylight offset with the
// two transitions that bound it. Offsets are east-positive, the opposite of
// the POSIX text, so that local = utc + offset everywhere in this module.
struct TimeZoneRule {
  int32_t stdOffsetSeconds = 0;               // Standard-time offset from UTC.
  int32_t dstOffsetSeconds = 0;               // Daylight offset; equals std without DST.
  bool hasDst = false;                        // True when the rule has transitions.
  TransitionRule dstStart;                    // Enters daylight time (in standard local time).
  TransitionRule dstEnd;                      // Leaves daylight time (in daylight local time).
  char stdName[kTimezoneNameLength] = "UTC";  // Abbreviation in standard time.
  char dstName[kTimezoneNameLength] = "";     // Abbreviation in daylight time.
};

// Parses a POSIX TZ string. Returns false, leaving *out untouched, on anything
// malformed or outside the supported grammar.
bool parsePosixTimeZone(const char* text, TimeZoneRule* out);

// True when daylight time is in effect at the given UTC instant.
bool isDaylightTimeAt(const TimeZoneRule& rule, uint32_t utc);

// Offset from UTC (east-positive seconds) in effect at the given UTC instant.
int32_t utcOffsetSecondsAt(const TimeZoneRule& rule, uint32_t utc);

// Local wall-clock seconds (same epoch arithmetic as RTClib's DateTime).
uint32_t localFromUtc(const TimeZoneRule& rule, uint32_t utc);

// The UTC instant for a local wall-clock time. A local time skipped by a
// spring-forward gap resolves to the later side (02:30 becomes 03:30 EDT); a
// local time repeated by a fall-back overlap resolves to its first occurrence.
uint32_t utcFromLocal(const TimeZoneRule& rule, uint32_t local);

// Finds the first offset change strictly after `utc`. Returns false for a rule
// without daylight time.
bool nextTransitionAfter(const TimeZoneRule& rule, uint32_t utc, uint32_t* atUtc);

// Zone abbreviation in effect at the given UTC instant ("EDT", "-03").
const char* abbreviationAt(const TimeZoneRule& rule, uint32_t utc);

// Writes a fixed-offset rule for an east-positive offset in minutes:
// -240 -> "<-04>4", 330 -> "<+0530>-5:30", 0 -> "UTC0". Used to carry a legacy
// numeric offset forward until a real zone is chosen.
void formatFixedOffsetRule(int offsetMinutes, char* out, size_t outSize);
