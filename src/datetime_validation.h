#pragma once

#include <RTClib.h>

// Calendar validation for the RTC's supported 2000-2099 range.
bool isValidDate(int year, int month, int day);
bool isValidTime(int hour, int minute, int second);
bool parseIsoDate(const char* text, int* year, int* month, int* day);
// Accepts HH:MM or HH:MM:SS; omitted seconds become zero.
bool parseClockTime(const char* text, int* hour, int* minute, int* second);
// Accepts YYYY-MM-DD HH:MM[:SS], with a space or T separator.
bool parseLocalDateTime(const char* text, DateTime& result);
