#include "datetime_validation.h"

#include <string.h>

namespace {

int digits(const char* text, size_t count) {
  int value = 0;
  for (size_t i = 0; i < count; ++i) {
    if ((text[i] < '0') || (text[i] > '9')) return -1;
    value = value * 10 + text[i] - '0';
  }
  return value;
}

}  // namespace

bool isValidDate(int year, int month, int day) {
  if ((year < 2000) || (year > 2099) || (month < 1) || (month > 12)) return false;
  static const uint8_t kDaysInMonth[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leapYear = ((year % 4 == 0) && (year % 100 != 0)) || (year % 400 == 0);
  const int lastDay = ((month == 2) && leapYear) ? 29 : kDaysInMonth[month - 1];
  return (day >= 1) && (day <= lastDay);
}

bool isValidTime(int hour, int minute, int second) {
  return (hour >= 0) && (hour <= 23) && (minute >= 0) && (minute <= 59) &&
         (second >= 0) && (second <= 59);
}

bool parseIsoDate(const char* text, int* year, int* month, int* day) {
  if ((text == nullptr) || (strlen(text) != 10) ||
      (text[4] != '-') || (text[7] != '-')) return false;
  *year = digits(text, 4);
  *month = digits(text + 5, 2);
  *day = digits(text + 8, 2);
  return isValidDate(*year, *month, *day);
}

bool parseClockTime(const char* text, int* hour, int* minute, int* second) {
  if (text == nullptr) return false;
  const size_t length = strlen(text);
  if (((length != 5) && (length != 8)) || (text[2] != ':') ||
      ((length == 8) && (text[5] != ':'))) return false;
  *hour = digits(text, 2);
  *minute = digits(text + 3, 2);
  *second = (length == 8) ? digits(text + 6, 2) : 0;
  return isValidTime(*hour, *minute, *second);
}

bool parseLocalDateTime(const char* text, DateTime& result) {
  if (text == nullptr) return false;
  const size_t length = strlen(text);
  if (((length != 16) && (length != 19)) ||
      ((text[10] != ' ') && (text[10] != 'T'))) return false;
  char date[11];
  memcpy(date, text, 10);
  date[10] = '\0';
  int year, month, day, hour, minute, second;
  if (!parseIsoDate(date, &year, &month, &day) ||
      !parseClockTime(text + 11, &hour, &minute, &second)) return false;
  result = DateTime(year, month, day, hour, minute, second);
  return true;
}
