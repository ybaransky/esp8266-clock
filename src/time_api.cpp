#include "time_api.h"

#include "clock_controller.h"
#include "config.h"
#include "config_api.h"
#include "config_validation.h"
#include "datetime_validation.h"
#include "log.h"
#include "network_time.h"
#include "rtc_ds3231.h"
#include "timezone_rule.h"

namespace {

constexpr uint32_t kEarliestSettableUtc = 1577836800;  // 2020-01-01; the RTC sync minimum.
constexpr uint32_t kLatestSettableUtc = 4102444799;    // 2099-12-31 23:59:59, the DS3231's range.

int32_t offsetMinutesAt(const TimeZoneRule& zone, uint32_t utc) {
  return utcOffsetSecondsAt(zone, utc) / 60;
}

// Writes date, time, and combined fields for a local wall-clock second.
void addLocalFields(JsonObject target, uint32_t local) {
  char dateTime[kLocalDateTimeLength];
  formatLocalDateTime(DateTime(local), dateTime, sizeof(dateTime));
  char date[11];
  strlcpy(date, dateTime, sizeof(date));
  // Non-const char* values, so ArduinoJson copies them out of these locals.
  target["date"] = date;
  target["time"] = dateTime + 11;
  target["dateTime"] = dateTime;
}

// Reads the request's timezone object. Returns false only for a present but
// invalid one; `present` reports whether it was supplied at all.
bool readTimezone(JsonVariantConst source, TimezoneConfig& timezone, bool& present) {
  present = !source.isNull();
  if (!present) return true;
  const char* rule = source["posix"] | "";
  TimeZoneRule parsed;
  if ((strlen(rule) >= sizeof(timezone.posix)) || !parsePosixTimeZone(rule, &parsed)) {
    return false;
  }
  strlcpy(timezone.posix, rule, sizeof(timezone.posix));
  sanitizePrintableText(source["name"] | "", timezone.name, sizeof(timezone.name));
  return true;
}

}  // namespace

// -----------------------------------------------------------------------------
// TimeApi
// -----------------------------------------------------------------------------

void TimeApi::handleGetTime() {
  const uint32_t utc = m_rtc.getUtcNow();
  const TimeZoneRule& zone = m_rtc.timeZone();
  JsonDocument doc;
  doc["utc"] = utc;
  addLocalFields(doc.as<JsonObject>(), localFromUtc(zone, utc));

  JsonObject timezone = doc["timezone"].to<JsonObject>();
  timezone["name"] = m_configManager.clockConfig().timezone.name;
  timezone["posix"] = m_configManager.clockConfig().timezone.posix;
  timezone["abbreviation"] = abbreviationAt(zone, utc);
  timezone["utcOffsetMinutes"] = offsetMinutesAt(zone, utc);

  uint32_t changeUtc = 0;
  if (nextTransitionAfter(zone, utc, &changeUtc)) {
    JsonObject next = doc["nextChange"].to<JsonObject>();
    next["utc"] = changeUtc;
    // The wall clock at the instant of the change, read with the old offset
    // ("02:00" on the night the clocks go back).
    char before[kLocalDateTimeLength];
    formatLocalDateTime(DateTime(changeUtc + utcOffsetSecondsAt(zone, changeUtc - 1)),
                        before, sizeof(before));
    next["localBefore"] = before;
    next["abbreviation"] = abbreviationAt(zone, changeUtc);
    next["utcOffsetMinutes"] = offsetMinutesAt(zone, changeUtc);
  }

  doc["rtcPresent"] = m_rtc.getStatus().present;
  doc["trusted"] = m_rtc.timeIsTrustworthy();
  JsonObject ntp = doc["ntp"].to<JsonObject>();
  ntp["active"] = m_networkTime.active();
  ntp["lastSyncUtc"] = m_networkTime.lastSyncUtc();
  m_responder.sendJsonDocument(200, doc);
}

// Accepts {utc} or {local: "YYYY-MM-DD HH:MM:SS"} and/or {timezone: {name,
// posix}}. Everything is validated before anything changes; the timezone is
// saved and applied first, so a local time is read in the zone just chosen.
void TimeApi::handleTimeSync() {
  JsonDocument doc;
  if (!m_responder.parseJsonBody(doc, "/api/time")) return;

  TimezoneConfig timezone = m_configManager.clockConfig().timezone;
  bool hasTimezone = false;
  if (!readTimezone(doc["timezone"], timezone, hasTimezone)) {
    LOG_PRINTLN("/api/time failed: invalid timezone rule");
    m_responder.sendJsonError(400, "Timezone rule is invalid");
    return;
  }

  const bool hasUtc = !doc["utc"].isNull();
  const bool hasLocal = !doc["local"].isNull();
  uint32_t utc = doc["utc"] | 0UL;
  DateTime local;
  if (hasUtc && ((utc < kEarliestSettableUtc) || (utc > kLatestSettableUtc))) {
    LOG_PRINTF("/api/time failed: utc %lu out of range", static_cast<unsigned long>(utc));
    m_responder.sendJsonError(400, "Time is out of range");
    return;
  }
  if (!hasUtc && hasLocal &&
      (!parseLocalDateTime(doc["local"] | "", local) || (local.year() < 2020))) {
    LOG_PRINTLN("/api/time failed: invalid local time");
    m_responder.sendJsonError(400, "Invalid time");
    return;
  }
  const bool hasTime = hasUtc || hasLocal;
  if (!hasTime && !hasTimezone) {
    m_responder.sendJsonError(400, "Time or timezone required");
    return;
  }
  if (hasTime && !m_rtc.getStatus().present) {
    LOG_PRINTLN("/api/time failed: no RTC present");
    m_responder.sendJsonError(503, "No RTC: time not set");
    return;
  }

  if (hasTimezone && !m_configApi.saveTimezone(timezone)) {
    m_responder.sendJsonError(500, "Configuration write failed");
    return;
  }
  if (hasTime) {
    if (!hasUtc) utc = utcFromLocal(m_rtc.timeZone(), local.unixtime());
    LOG_PRINTF("Time sync requested: utc=%lu", static_cast<unsigned long>(utc));
    m_clockController.setTime(utc, "browser time sync");
  }
  const char* message = (hasTime && hasTimezone) ? "Time and timezone saved"
                        : hasTime                ? "Time set"
                                                 : "Timezone saved";
  JsonDocument response;
  response["message"] = message;
  m_responder.sendJsonDocument(200, response);
}
