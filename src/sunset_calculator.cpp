#include "sunset_calculator.h"

DateTime calculateSunset(const DateTime& localDate, const Location& location) {
  (void)location;
  return DateTime(localDate.year(), localDate.month(), localDate.day(), 18, 0, 0);
}
