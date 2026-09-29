#include "timezone_rule.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

constexpr int32_t kSecondsPerDay = 86400;
constexpr int32_t kSecondsPerHour = 3600;
constexpr int32_t kMaxOffsetHours = 24;       // POSIX bound for a UTC offset.
constexpr int32_t kMaxTransitionHours = 167;  // POSIX bound for a transition time.
constexpr int32_t kDefaultTransitionSeconds = 2 * kSecondsPerHour;  // "/2" when omitted.
constexpr size_t kMinAlphaNameLength = 3;     // POSIX minimum for an unquoted name.

// -- Calendar arithmetic (proleptic Gregorian, days since 1970-01-01) --------

// Howard Hinnant's days_from_civil; exact for every year this firmware sees.
int32_t daysFromCivil(int32_t year, uint32_t month, uint32_t day) {
  year -= (month <= 2) ? 1 : 0;
  const int32_t era = ((year >= 0) ? year : (year - 399)) / 400;
  const uint32_t yearOfEra = static_cast<uint32_t>(year - era * 400);
  const uint32_t dayOfYear = (153 * (month + ((month > 2) ? -3 : 9)) + 2) / 5 + day - 1;
  const uint32_t dayOfEra = yearOfEra * 365 + yearOfEra / 4 - yearOfEra / 100 + dayOfYear;
  return era * 146097 + static_cast<int32_t>(dayOfEra) - 719468;
}

int32_t yearFromDays(int32_t days) {
  days += 719468;
  const int32_t era = ((days >= 0) ? days : (days - 146096)) / 146097;
  const uint32_t dayOfEra = static_cast<uint32_t>(days - era * 146097);
  const uint32_t yearOfEra =
      (dayOfEra - dayOfEra / 1460 + dayOfEra / 36524 - dayOfEra / 146096) / 365;
  const uint32_t dayOfYear = dayOfEra - (365 * yearOfEra + yearOfEra / 4 - yearOfEra / 100);
  const uint32_t monthIndex = (5 * dayOfYear + 2) / 153;
  const uint32_t month = monthIndex + ((monthIndex < 10) ? 3 : -9);
  return static_cast<int32_t>(yearOfEra) + era * 400 + ((month <= 2) ? 1 : 0);
}

bool isLeapYear(int32_t year) {
  return ((year % 4) == 0) && (((year % 100) != 0) || ((year % 400) == 0));
}

uint32_t daysInMonth(int32_t year, uint32_t month) {
  static const uint8_t kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  return ((month == 2) && isLeapYear(year)) ? 29 : kDays[month - 1];
}

// 0 = Sunday; 1970-01-01 was a Thursday.
uint32_t weekdayFromDays(int32_t days) {
  return static_cast<uint32_t>(((days % 7) + 7 + 4) % 7);
}

// Local wall-clock seconds at which a transition happens in the given year.
int64_t transitionLocalSeconds(int32_t year, const TransitionRule& transition) {
  const int32_t firstOfMonth = daysFromCivil(year, transition.month, 1);
  uint32_t dayOffset = (transition.weekday + 7 - weekdayFromDays(firstOfMonth)) % 7 +
                       (transition.week - 1) * 7u;
  // Only week 5 ("last") can overshoot; step back into the month.
  while (dayOffset >= daysInMonth(year, transition.month)) dayOffset -= 7;
  return static_cast<int64_t>(firstOfMonth + static_cast<int32_t>(dayOffset)) * kSecondsPerDay +
         transition.timeSeconds;
}

// DST start and end for one year, as UTC instants. The start is expressed in
// standard local time and the end in daylight local time, per POSIX.
void transitionsForYear(const TimeZoneRule& rule, int32_t year,
                        int64_t* startUtc, int64_t* endUtc) {
  *startUtc = transitionLocalSeconds(year, rule.dstStart) - rule.stdOffsetSeconds;
  *endUtc = transitionLocalSeconds(year, rule.dstEnd) - rule.dstOffsetSeconds;
}

int32_t standardYearAt(const TimeZoneRule& rule, int64_t utc) {
  const int64_t local = utc + rule.stdOffsetSeconds;
  const int64_t days = (local >= 0) ? (local / kSecondsPerDay)
                                    : ((local - kSecondsPerDay + 1) / kSecondsPerDay);
  return yearFromDays(static_cast<int32_t>(days));
}

bool isDaylightAt(const TimeZoneRule& rule, int64_t utc) {
  if (!rule.hasDst) return false;
  int64_t start = 0;
  int64_t end = 0;
  transitionsForYear(rule, standardYearAt(rule, utc), &start, &end);
  // Southern-hemisphere rules start late in the year and end early in it.
  return (start < end) ? ((utc >= start) && (utc < end))
                       : ((utc < end) || (utc >= start));
}

int32_t offsetAt(const TimeZoneRule& rule, int64_t utc) {
  return isDaylightAt(rule, utc) ? rule.dstOffsetSeconds : rule.stdOffsetSeconds;
}

// -- POSIX TZ text parsing -----------------------------------------------------

// Walks a POSIX TZ string once, left to right. Every read either consumes a
// complete token or reports failure; nothing is committed until the whole
// string has parsed.
class PosixTzParser {
 public:
  explicit PosixTzParser(const char* text) : m_cursor(text) {}

  bool parse(TimeZoneRule* out) {
    TimeZoneRule rule;
    int32_t posixStdOffset = 0;
    if (!parseName(rule.stdName) || !parseSignedTime(kMaxOffsetHours, &posixStdOffset)) {
      return false;
    }
    rule.stdOffsetSeconds = -posixStdOffset;
    rule.dstOffsetSeconds = rule.stdOffsetSeconds;
    if (atEnd()) {
      *out = rule;
      return true;
    }

    if (!parseName(rule.dstName)) return false;
    rule.dstOffsetSeconds = rule.stdOffsetSeconds + kSecondsPerHour;
    if (startsNumber()) {
      int32_t posixDstOffset = 0;
      if (!parseSignedTime(kMaxOffsetHours, &posixDstOffset)) return false;
      rule.dstOffsetSeconds = -posixDstOffset;
    }
    // A DST name without explicit rules means "implementation-defined" in
    // POSIX; refusing it is better than guessing another country's dates.
    if (!consume(',') || !parseTransition(&rule.dstStart) ||
        !consume(',') || !parseTransition(&rule.dstEnd) || !atEnd()) {
      return false;
    }
    rule.hasDst = true;
    *out = rule;
    return true;
  }

 private:
  bool atEnd() const { return *m_cursor == '\0'; }

  bool consume(char expected) {
    if (*m_cursor != expected) return false;
    ++m_cursor;
    return true;
  }

  bool startsNumber() const {
    return ((*m_cursor >= '0') && (*m_cursor <= '9')) || (*m_cursor == '+') ||
           (*m_cursor == '-');
  }

  // A name is either <...> (letters, digits, + and -) or 3+ letters.
  bool parseName(char (&name)[kTimezoneNameLength]) {
    size_t length = 0;
    if (consume('<')) {
      while ((*m_cursor != '>') && (*m_cursor != '\0')) {
        const char c = *m_cursor;
        const bool allowed = ((c >= 'A') && (c <= 'Z')) || ((c >= 'a') && (c <= 'z')) ||
                             ((c >= '0') && (c <= '9')) || (c == '+') || (c == '-');
        if (!allowed || (length + 1 >= kTimezoneNameLength)) return false;
        name[length++] = c;
        ++m_cursor;
      }
      if (!consume('>') || (length == 0)) return false;
    } else {
      while (((*m_cursor >= 'A') && (*m_cursor <= 'Z')) ||
             ((*m_cursor >= 'a') && (*m_cursor <= 'z'))) {
        if (length + 1 >= kTimezoneNameLength) return false;
        name[length++] = *m_cursor++;
      }
      if (length < kMinAlphaNameLength) return false;
    }
    name[length] = '\0';
    return true;
  }

  bool parseNumber(uint32_t maxValue, uint32_t* value) {
    if ((*m_cursor < '0') || (*m_cursor > '9')) return false;
    uint32_t result = 0;
    while ((*m_cursor >= '0') && (*m_cursor <= '9')) {
      result = result * 10 + static_cast<uint32_t>(*m_cursor++ - '0');
      if (result > maxValue) return false;
    }
    *value = result;
    return true;
  }

  // [+-]hh[:mm[:ss]] in seconds, keeping the text's sign.
  bool parseSignedTime(int32_t maxHours, int32_t* seconds) {
    const bool negative = consume('-');
    if (!negative) consume('+');
    uint32_t hours = 0;
    uint32_t minutes = 0;
    uint32_t secs = 0;
    if (!parseNumber(static_cast<uint32_t>(maxHours), &hours)) return false;
    if (consume(':')) {
      if (!parseNumber(59, &minutes)) return false;
      if (consume(':') && !parseNumber(59, &secs)) return false;
    }
    const int32_t total = static_cast<int32_t>(hours * 3600 + minutes * 60 + secs);
    *seconds = negative ? -total : total;
    return true;
  }

  // Mm.w.d[/time]
  bool parseTransition(TransitionRule* transition) {
    uint32_t month = 0;
    uint32_t week = 0;
    uint32_t weekday = 0;
    if (!consume('M') || !parseNumber(12, &month) || (month < 1) || !consume('.') ||
        !parseNumber(5, &week) || (week < 1) || !consume('.') || !parseNumber(6, &weekday)) {
      return false;
    }
    int32_t timeSeconds = kDefaultTransitionSeconds;
    if (consume('/') && !parseSignedTime(kMaxTransitionHours, &timeSeconds)) return false;
    transition->month = static_cast<uint8_t>(month);
    transition->week = static_cast<uint8_t>(week);
    transition->weekday = static_cast<uint8_t>(weekday);
    transition->timeSeconds = timeSeconds;
    return true;
  }

  const char* m_cursor;  // Next unread character of the rule text.
};

}  // namespace

bool parsePosixTimeZone(const char* text, TimeZoneRule* out) {
  if ((text == nullptr) || (out == nullptr)) return false;
  return PosixTzParser(text).parse(out);
}

bool isDaylightTimeAt(const TimeZoneRule& rule, uint32_t utc) {
  return isDaylightAt(rule, utc);
}

int32_t utcOffsetSecondsAt(const TimeZoneRule& rule, uint32_t utc) {
  return offsetAt(rule, utc);
}

uint32_t localFromUtc(const TimeZoneRule& rule, uint32_t utc) {
  return static_cast<uint32_t>(static_cast<int64_t>(utc) + offsetAt(rule, utc));
}

uint32_t utcFromLocal(const TimeZoneRule& rule, uint32_t local) {
  const int64_t localSeconds = local;
  const int64_t asStandard = localSeconds - rule.stdOffsetSeconds;
  if (!rule.hasDst) return static_cast<uint32_t>(asStandard);

  const int64_t asDaylight = localSeconds - rule.dstOffsetSeconds;
  const bool standardFits = (asStandard + offsetAt(rule, asStandard)) == localSeconds;
  const bool daylightFits = (asDaylight + offsetAt(rule, asDaylight)) == localSeconds;
  if (standardFits && daylightFits) {
    return static_cast<uint32_t>((asStandard < asDaylight) ? asStandard : asDaylight);
  }
  if (standardFits) return static_cast<uint32_t>(asStandard);
  if (daylightFits) return static_cast<uint32_t>(asDaylight);
  // Skipped by a gap: read the time with the smaller (pre-gap) offset, which
  // lands the same distance past the transition.
  const int32_t smallerOffset = (rule.stdOffsetSeconds < rule.dstOffsetSeconds)
                                    ? rule.stdOffsetSeconds
                                    : rule.dstOffsetSeconds;
  return static_cast<uint32_t>(localSeconds - smallerOffset);
}

bool nextTransitionAfter(const TimeZoneRule& rule, uint32_t utc, uint32_t* atUtc) {
  if (!rule.hasDst || (atUtc == nullptr)) return false;
  const int32_t year = standardYearAt(rule, utc);
  int64_t best = 0;
  bool found = false;
  for (int32_t candidateYear = year - 1; candidateYear <= year + 1; ++candidateYear) {
    int64_t instants[2] = {0, 0};
    transitionsForYear(rule, candidateYear, &instants[0], &instants[1]);
    for (const int64_t instant : instants) {
      if ((instant > utc) && (!found || (instant < best))) {
        best = instant;
        found = true;
      }
    }
  }
  if (found) *atUtc = static_cast<uint32_t>(best);
  return found;
}

const char* abbreviationAt(const TimeZoneRule& rule, uint32_t utc) {
  return isDaylightAt(rule, utc) ? rule.dstName : rule.stdName;
}

void formatFixedOffsetRule(int offsetMinutes, char* out, size_t outSize) {
  if ((out == nullptr) || (outSize == 0)) return;
  if (offsetMinutes == 0) {
    snprintf(out, outSize, "UTC0");
    return;
  }
  const char nameSign = (offsetMinutes > 0) ? '+' : '-';
  // POSIX offsets count west as positive, so the text sign is inverted.
  const char* posixSign = (offsetMinutes > 0) ? "-" : "";
  const int magnitude = abs(offsetMinutes);
  const int hours = magnitude / 60;
  const int minutes = magnitude % 60;
  if (minutes == 0) {
    snprintf(out, outSize, "<%c%02d>%s%d", nameSign, hours, posixSign, hours);
  } else {
    snprintf(out, outSize, "<%c%02d%02d>%s%d:%02d", nameSign, hours, minutes, posixSign,
             hours, minutes);
  }
}
