#include "time_api.h"

#include "clock_controller.h"
#include "log.h"
#include "rtc_ds3231.h"

// -----------------------------------------------------------------------------
// TimeApi
// -----------------------------------------------------------------------------

bool TimeApi::parseJsonBody(JsonDocument& doc) {
  const DeserializationError error = deserializeJson(doc, server_.arg("plain"));
  if (!error) return true;

  LOG_PRINTF("/api/time failed: invalid JSON: %s", error.c_str());
  responder_.sendJson(400, "{\"error\":\"Invalid JSON\"}");
  return false;
}

void TimeApi::handleGetTime() {
  const DateTime dt = rtc_.getNow();
  char buffer[96];
  snprintf(buffer, sizeof(buffer),
           "{\"date\":\"%04d-%02d-%02d\",\"time\":\"%02d:%02d:%02d\",\"dateTime\":\"%04d-%02d-%02d %02d:%02d:%02d\"}",
           dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second(),
           dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second());
  responder_.sendJson(200, buffer);
}

void TimeApi::handleTimeSync() {
  JsonDocument doc;
  if (!parseJsonBody(doc)) return;

  const int year = doc["year"] | 0;
  const int month = doc["month"] | 0;
  const int day = doc["day"] | 0;
  const int hour = doc["hour"] | 0;
  const int minute = doc["minute"] | 0;
  const int second = doc["second"] | 0;
  if ((year < 2020) || (year > 2099) || (month < 1) || (month > 12) ||
      (day < 1) || (day > 31) || (hour < 0) || (hour > 23) ||
      (minute < 0) || (minute > 59) || (second < 0) || (second > 59)) {
    LOG_PRINTF("/api/time failed: invalid time %04d-%02d-%02d %02d:%02d:%02d",
               year, month, day, hour, minute, second);
    responder_.sendJson(400, "{\"error\":\"Invalid time\"}");
    return;
  }

  LOG_PRINTF("Browser time sync requested: %04d-%02d-%02d %02d:%02d:%02d",
             year, month, day, hour, minute, second);
  clockController_.setTime(DateTime(year, month, day, hour, minute, second));
  responder_.sendJson(200, "{\"message\":\"RTC synced\"}");
}
