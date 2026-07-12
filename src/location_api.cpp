#include "location_api.h"

#include <math.h>

#include "config_validation.h"
#include "log.h"
#include "sunset_calculator.h"
#include "zipcode.h"

namespace {

bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int daysInMonth(int year, int month) {
  static const uint8_t kDaysByMonth[] = {
      31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) return 0;
  if (month == 2 && isLeapYear(year)) return 29;
  return kDaysByMonth[month - 1];
}

bool parseIsoDate(const char* text, int* year, int* month, int* day) {
  int consumed = 0;
  if (text == nullptr ||
      sscanf(text, "%d-%d-%d%n", year, month, day, &consumed) != 3 ||
      text[consumed] != '\0') {
    return false;
  }
  return *year >= 2020 && *year <= 2099 &&
         *month >= 1 && *month <= 12 &&
         *day >= 1 && *day <= daysInMonth(*year, *month);
}

bool parseTimeOfDay(const char* text, int* hour, int* minute, int* second) {
  int consumed = 0;
  *second = 0;
  if (text == nullptr) return false;

  const int matchedWithSeconds =
      sscanf(text, "%d:%d:%d%n", hour, minute, second, &consumed);
  if (matchedWithSeconds != 3 || text[consumed] != '\0') {
    consumed = 0;
    if (sscanf(text, "%d:%d%n", hour, minute, &consumed) != 2 ||
        text[consumed] != '\0') {
      return false;
    }
  }
  return *hour >= 0 && *hour <= 23 &&
         *minute >= 0 && *minute <= 59 &&
         *second >= 0 && *second <= 59;
}

}  // namespace

// -----------------------------------------------------------------------------
// LocationApi
// -----------------------------------------------------------------------------

bool LocationApi::parseJsonBody(JsonDocument& doc, const char* route) {
  const DeserializationError error = deserializeJson(doc, server_.arg("plain"));
  if (!error) return true;

  LOG_PRINTF("%s failed: invalid JSON: %s\n", route, error.c_str());
  responder_.sendJson(400, "{\"error\":\"Invalid JSON\"}");
  return false;
}

void LocationApi::handleZipcodeLookup() {
  const String zipcode = server_.arg("zip");
  LOG_PRINTF("/api/zipcode/lookup requested: zip=\"%s\"\n", zipcode.c_str());
  if (!isValidZipcode(zipcode.c_str())) {
    LOG_PRINTF("/api/zipcode/lookup failed: invalid zipcode=\"%s\"\n",
               zipcode.c_str());
    responder_.sendJson(400, "{\"error\":\"ZIP code must be 5 digits\"}");
    return;
  }

  ZipcodeLocation location;
  if (!zipcodeLookupLocation(zipcode.c_str(), &location)) {
    LOG_PRINTF("/api/zipcode/lookup failed: zip not found or unreadable: \"%s\"\n",
               zipcode.c_str());
    responder_.sendJson(404, "{\"error\":\"ZIP code not found\"}");
    return;
  }

  LOG_PRINTF("/api/zipcode/lookup success: zip=\"%s\" lat=%.6f lon=%.6f\n",
             location.zipcode, location.latitude, location.longitude);
  char json[96];
  snprintf(json, sizeof(json),
           "{\"zipcode\":\"%s\",\"latitude\":%.6f,\"longitude\":%.6f}",
           location.zipcode, location.latitude, location.longitude);
  responder_.sendJson(200, json);
}

void LocationApi::handleSunset() {
  JsonDocument doc;
  if (!parseJsonBody(doc, "/api/sunset")) return;

  const float latitude = doc["location"]["latitude"] | NAN;
  const float longitude = doc["location"]["longitude"] | NAN;
  const char* dateTextArg = doc["time"]["date"] | "";
  const char* timeTextArg = doc["time"]["time"] | "";
  LOG_PRINTF("/api/sunset request: raw date=\"%s\" raw time=\"%s\" lat=%.6f lon=%.6f offset=%d dst=%s\n",
             dateTextArg, timeTextArg, latitude, longitude,
             doc["time"]["timezone"]["utcOffsetMinutes"] | 0,
             (doc["time"]["dst"] | false) ? "true" : "false");

  if (!isfinite(latitude) || latitude < -90.0f || latitude > 90.0f ||
      !isfinite(longitude) || longitude < -180.0f || longitude > 180.0f) {
    LOG_PRINTF("/api/sunset failed: invalid coordinates lat=%.6f lon=%.6f\n",
               latitude, longitude);
    responder_.sendJson(400, "{\"error\":\"Latitude or longitude is invalid\"}");
    return;
  }

  int year = 0, month = 0, day = 0;
  if (!parseIsoDate(dateTextArg, &year, &month, &day)) {
    LOG_PRINTF("/api/sunset failed: invalid date=\"%s\"\n", dateTextArg);
    responder_.sendJson(400, "{\"error\":\"Date is invalid\"}");
    return;
  }

  int hour = 0, minute = 0, second = 0;
  if (!parseTimeOfDay(timeTextArg, &hour, &minute, &second)) {
    LOG_PRINTF("/api/sunset failed: invalid time=\"%s\"\n", timeTextArg);
    responder_.sendJson(400, "{\"error\":\"Time is invalid\"}");
    return;
  }

  const int utcOffsetMinutes = doc["time"]["timezone"]["utcOffsetMinutes"] | 0;
  if (utcOffsetMinutes < -840 || utcOffsetMinutes > 840) {
    LOG_PRINTF("/api/sunset failed: invalid UTC offset=%d\n", utcOffsetMinutes);
    responder_.sendJson(400, "{\"error\":\"UTC offset is invalid\"}");
    return;
  }

  char timezone[40];
  sanitizePrintableText(doc["time"]["timezone"]["name"] | "",
                        timezone, sizeof(timezone));
  const bool dst = doc["time"]["dst"] | false;
  const Location location{latitude, longitude,
                          static_cast<int16_t>(utcOffsetMinutes)};
  const DateTime calculatorDate(year, month, day, 0, 0, 0);
  LOG_PRINTF("Sunset calculator args: localDate=%04d-%02d-%02d latitude=%.6f longitude=%.6f utcOffsetMinutes=%d\n",
             calculatorDate.year(), calculatorDate.month(), calculatorDate.day(),
             location.latitude, location.longitude, location.utcOffsetMinutes);
  const DateTime sunset = calculateSunset(calculatorDate, location);
  LOG_PRINTF("Sunset calculator response: localSunset=%04d-%02d-%02d %02d:%02d:%02d\n",
             sunset.year(), sunset.month(), sunset.day(),
             sunset.hour(), sunset.minute(), sunset.second());
  LOG_PRINTF("/api/sunset success: lat=%.6f lon=%.6f date=%04d-%02d-%02d offset=%d dst=%s sunset=%02d:%02d:%02d\n",
             latitude, longitude, year, month, day, utcOffsetMinutes,
             dst ? "true" : "false",
             sunset.hour(), sunset.minute(), sunset.second());

  char dateText[16], timeText[12], dateTimeText[28];
  snprintf(dateText, sizeof(dateText), "%04d-%02d-%02d",
           sunset.year(), sunset.month(), sunset.day());
  snprintf(timeText, sizeof(timeText), "%02d:%02d:%02d",
           sunset.hour(), sunset.minute(), sunset.second());
  snprintf(dateTimeText, sizeof(dateTimeText), "%s %s", dateText, timeText);

  JsonDocument response;
  response["date"] = dateText;
  response["time"] = timeText;
  response["dateTime"] = dateTimeText;
  response["timezone"] = timezone;
  response["utcOffsetMinutes"] = utcOffsetMinutes;
  response["dst"] = dst;
  responder_.sendJsonDocument(200, response);
}
