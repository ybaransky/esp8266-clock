#pragma once

#include <stddef.h>

#include <RTClib.h>

// Calendar validation for the RTC's supported 2000-2099 range.
bool isValidDate(int year, int month, int day);
bool isValidTime(int hour, int minute, int second);
bool parseIsoDate(const char* text, int* year, int* month, int* day);
// Accepts HH:MM or HH:MM:SS; omitted seconds become zero.
bool parseClockTime(const char* text, int* hour, int* minute, int* second);
// Accepts YYYY-MM-DD HH:MM[:SS], with a space or T separator.
bool parseLocalDateTime(const char* text, DateTime& result);
// Writes the canonical YYYY-MM-DD HH:MM:SS form parseLocalDateTime accepts, so
// the two round-trip. Needs kLocalDateTimeLength bytes; a shorter buffer is
// truncated rather than overrun, and truncated output will not parse back.
static constexpr size_t kLocalDateTimeLength = 20;
void formatLocalDateTime(const DateTime& value, char* out, size_t outSize);
