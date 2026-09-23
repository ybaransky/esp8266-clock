#pragma once
// Minimal host stand-in for RTClib's DateTime, for the native test env only.
//
// The firmware's pure modules (schedule, display_format, datetime_validation)
// use DateTime purely as a calendar value type: construct from a Unix second or
// from Y/M/D H:M:S, then read the fields back. That is the whole surface
// reproduced here. No I2C, no chip, no RTClib.
//
// Semantics match RTClib where it matters: unixtime() is seconds since the 1970
// epoch, and dayOfTheWeek() is 0 = Sunday.

#include <stdint.h>

namespace rtclib_stub {

// Days since the 1970 epoch for a proleptic Gregorian Y/M/D (Howard Hinnant's
// days_from_civil). Valid well beyond the 2000-2099 range the firmware allows.
inline int32_t daysFromCivil(int32_t year, uint32_t month, uint32_t day) {
  year -= month <= 2;
  const int32_t era = (year >= 0 ? year : year - 399) / 400;
  const uint32_t yoe = static_cast<uint32_t>(year - era * 400);
  const uint32_t doy = (153u * (month + (month > 2 ? -3 : 9)) + 2u) / 5u + day - 1u;
  const uint32_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  return era * 146097 + static_cast<int32_t>(doe) - 719468;
}

// Inverse of daysFromCivil.
inline void civilFromDays(int32_t days, int* year, uint8_t* month, uint8_t* day) {
  days += 719468;
  const int32_t era = (days >= 0 ? days : days - 146096) / 146097;
  const uint32_t doe = static_cast<uint32_t>(days - era * 146097);
  const uint32_t yoe =
      (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
  const int32_t y = static_cast<int32_t>(yoe) + era * 400;
  const uint32_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
  const uint32_t mp = (5 * doy + 2) / 153;
  const uint32_t d = doy - (153 * mp + 2) / 5 + 1;
  const uint32_t m = mp + (mp < 10 ? 3 : -9);
  *year = static_cast<int>(y + (m <= 2));
  *month = static_cast<uint8_t>(m);
  *day = static_cast<uint8_t>(d);
}

}  // namespace rtclib_stub

class DateTime {
 public:
  DateTime() : unix_(0) { split(); }

  explicit DateTime(uint32_t unixSeconds) : unix_(unixSeconds) { split(); }

  DateTime(int year, uint8_t month, uint8_t day, uint8_t hour = 0,
           uint8_t minute = 0, uint8_t second = 0) {
    const int32_t days = rtclib_stub::daysFromCivil(year, month, day);
    unix_ = static_cast<uint32_t>(days) * 86400UL + hour * 3600UL +
            minute * 60UL + second;
    split();
  }

  uint32_t unixtime() const { return unix_; }
  int year() const { return year_; }
  uint8_t month() const { return month_; }
  uint8_t day() const { return day_; }
  uint8_t hour() const { return hour_; }
  uint8_t minute() const { return minute_; }
  uint8_t second() const { return second_; }
  // 0 = Sunday, matching RTClib. 1970-01-01 was a Thursday.
  uint8_t dayOfTheWeek() const {
    return static_cast<uint8_t>((unix_ / 86400UL + 4UL) % 7UL);
  }

 private:
  void split() {
    const int32_t days = static_cast<int32_t>(unix_ / 86400UL);
    const uint32_t secondOfDay = unix_ % 86400UL;
    rtclib_stub::civilFromDays(days, &year_, &month_, &day_);
    hour_ = static_cast<uint8_t>(secondOfDay / 3600UL);
    minute_ = static_cast<uint8_t>((secondOfDay % 3600UL) / 60UL);
    second_ = static_cast<uint8_t>(secondOfDay % 60UL);
  }

  uint32_t unix_;
  int year_ = 1970;
  uint8_t month_ = 1;
  uint8_t day_ = 1;
  uint8_t hour_ = 0;
  uint8_t minute_ = 0;
  uint8_t second_ = 0;
};
