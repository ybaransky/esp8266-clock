#include "config_api.h"

#include <ArduinoJson.h>
#include <math.h>

#include "clock_state.h"
#include "config.h"
#include "config_serializer.h"
#include "config_validation.h"
#include "format.h"
#include "log.h"
#include "rtc_ds3231.h"
#include "sunset_calculator.h"
#include "zipcode.h"

namespace {

constexpr uint32_t kRebootDelayMs = 1500;

bool isLeapYear(int year) {
  return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

int daysInMonth(int year, int month) {
  static const uint8_t kDaysByMonth[] = {
      31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 1 || month > 12) {
    return 0;
  }
  if (month == 2 && isLeapYear(year)) {
    return 29;
  }
  return kDaysByMonth[month - 1];
}

bool parseIsoDate(const char* text, int* year, int* month, int* day) {
  int consumed = 0;
  if (text == nullptr || sscanf(text, "%d-%d-%d%n",
                                year,
                                month,
                                day,
                                &consumed) != 3 ||
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
  if (text == nullptr) {
    return false;
  }
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

// Returns false and logs an error if zipcode is present but invalid.
bool applyZipcode(const char* label, const char* zipcode,
                  char* dest, size_t destSize) {
  if (zipcode == nullptr || (zipcode[0] != '\0' && !isValidZipcode(zipcode))) {
    LOG_PRINTF("/api/config save failed: invalid %s zipcode=\"%s\"\n",
               label, zipcode == nullptr ? "(null)" : zipcode);
    return false;
  }
  snprintf(dest, destSize, "%s", zipcode);
  return true;
}

}  // namespace

// -- handleSaveConfig sub-helpers ----------------------------------------------

// Returns false if activeMode is provided but invalid.
static bool applyModeAndFormats(JsonVariant display, JsonVariant modes, ClockConfig& cfg) {
  if (!display["activeMode"].isNull()) {
    PersistentMode nextMode;
    const String mode = display["activeMode"] | "";
    if (!persistentModeFromName(mode, &nextMode)) {
      LOG_PRINTF("/api/config save failed: invalid activeMode=\"%s\"\n", mode.c_str());
      return false;
    }
    cfg.activeMode = nextMode;
  }
  if (!modes["countdown"]["format"].isNull()) {
    cfg.countdownFmt = sanitizeFormatIndex(kFmtGroupCountdown,
                                           modes["countdown"]["format"].as<int>(),
                                           cfg.countdownFmt);
  }
  if (!modes["countup"]["format"].isNull()) {
    cfg.countupFmt = sanitizeFormatIndex(kFmtGroupCountUp,
                                         modes["countup"]["format"].as<int>(),
                                         cfg.countupFmt);
  }
  if (!modes["clock"]["format"].isNull()) {
    cfg.clockFmt = sanitizeFormatIndex(kFmtGroupClock,
                                       modes["clock"]["format"].as<int>(),
                                       cfg.clockFmt);
  }
  if (!modes["friday"]["clockFormat"].isNull()) {
    cfg.fridayClockFmt = sanitizeFormatIndex(kFmtGroupClock,
                                             modes["friday"]["clockFormat"].as<int>(),
                                             cfg.fridayClockFmt);
  }
  if (!modes["friday"]["toFridaySunsetFormat"].isNull()) {
    cfg.fridayToFridaySunsetFmt = sanitizeFormatIndex(kFmtGroupCountdown,
                                                      modes["friday"]["toFridaySunsetFormat"].as<int>(),
                                                      cfg.fridayToFridaySunsetFmt);
  }
  if (!modes["friday"]["toSaturdaySunsetFormat"].isNull()) {
    cfg.fridayToSatSunsetFmt = sanitizeFormatIndex(kFmtGroupCountdown,
                                                   modes["friday"]["toSaturdaySunsetFormat"].as<int>(),
                                                   cfg.fridayToSatSunsetFmt);
  }
  if (!display["brightness"].isNull()) {
    cfg.brightness = sanitizeBrightness(display["brightness"].as<int>());
  }
  if (!display["clock12Hour"].isNull()) {
    cfg.clockUse12Hour = display["clock12Hour"].as<bool>();
  }
  if (!modes["countdown"]["end"].isNull()) {
    snprintf(cfg.countdownDatetime, sizeof(cfg.countdownDatetime),
             "%s", modes["countdown"]["end"].as<const char*>());
  }
  if (!modes["countup"]["start"].isNull()) {
    snprintf(cfg.countupDatetime, sizeof(cfg.countupDatetime),
             "%s", modes["countup"]["start"].as<const char*>());
  }
  return true;
}

static void applyMessageFields(JsonVariant messages, ClockConfig& cfg) {
  if (!messages["splash"].isNull()) {
    sanitizeDisplayMessage(messages["splash"].as<const char*>(),
                           cfg.splashMessage, sizeof(cfg.splashMessage));
  }
  if (!messages["final"].isNull()) {
    sanitizeDisplayMessage(messages["final"].as<const char*>(),
                           cfg.finalMessage, sizeof(cfg.finalMessage));
  }
}

// Returns false if a zipcode field is present but malformed.
static bool applyLocationFields(JsonVariant location, ClockConfig& cfg) {
  if (!location["latitude"].isNull()) {
    cfg.location.latitude = location["latitude"].as<float>();
  }
  if (!location["longitude"].isNull()) {
    cfg.location.longitude = location["longitude"].as<float>();
  }
  if (!location["zipcode"].isNull()) {
    if (!applyZipcode("location", location["zipcode"].as<const char*>(),
                      cfg.location.zipcode, sizeof(cfg.location.zipcode))) {
      return false;
    }
  }
  return true;
}

// Returns false if a zipcode field is present but malformed.
static bool applySunsetFields(JsonVariant sunset, ClockConfig& cfg) {
  if (!sunset["latitude"].isNull()) {
    cfg.sunsetTest.latitude = sunset["latitude"].as<float>();
  }
  if (!sunset["longitude"].isNull()) {
    cfg.sunsetTest.longitude = sunset["longitude"].as<float>();
  }
  if (!sunset["zipcode"].isNull()) {
    if (!applyZipcode("sunset", sunset["zipcode"].as<const char*>(),
                      cfg.sunsetTest.zipcode, sizeof(cfg.sunsetTest.zipcode))) {
      return false;
    }
  }
  return true;
}

static void applyTimezoneFields(JsonVariant time, ClockConfig& cfg) {
  JsonVariant timezone = time["timezone"];
  if (!timezone["name"].isNull()) {
    sanitizePrintableText(timezone["name"].as<const char*>(),
                          cfg.timezone, sizeof(cfg.timezone));
  }
  if (!timezone["utcOffsetMinutes"].isNull()) {
    cfg.utcOffsetMinutes =
        sanitizeUtcOffsetMinutes(timezone["utcOffsetMinutes"].as<int>());
  }
  if (!time["dst"].isNull()) {
    cfg.dst = time["dst"].as<bool>();
  }
}

// -- API handlers --------------------------------------------------------------

bool ConfigApi::parseJsonBody(JsonDocument& doc, const char* route) {
  DeserializationError err = deserializeJson(doc, server_.arg("plain"));
  if (err) {
    LOG_PRINTF("%s failed: invalid JSON: %s\n", route, err.c_str());
    responder_.sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return false;
  }
  return true;
}

void ConfigApi::handleDemoTest() {
  if (server_.hasArg("plain") && server_.arg("plain").length() > 0) {
    JsonDocument doc;
    if (!parseJsonBody(doc, "/api/demo/test")) return;
    JsonVariant finalMessage = doc["display"]["messages"]["final"];
    if (!finalMessage.isNull()) {
      ClockConfig cfg = configManager.loadClockConfig();
      sanitizeDisplayMessage(finalMessage.as<const char*>(),
                             cfg.finalMessage,
                             sizeof(cfg.finalMessage));
      clockApplySettings(cfg);
    }
  }

  clockTriggerDemo();
  responder_.sendJson(200, "{\"preview_ms\":10000}");
}

void ConfigApi::handleMessageTest() {
  JsonDocument doc;
  if (!parseJsonBody(doc, "/api/message/test")) return;

  char message[64];
  sanitizeDisplayMessage(doc["message"] | "", message, sizeof(message));
  clockShowMessagePreview(message);
  responder_.sendJson(200, "{\"message\":\"Previewing message\",\"preview_ms\":5000}");
}

void ConfigApi::handleSetMode() {
  JsonDocument doc;
  if (!parseJsonBody(doc, "/api/mode")) return;

  PersistentMode nextMode;
  const String mode = doc["mode"] | "";
  if (!persistentModeFromName(mode, &nextMode)) {
    LOG_PRINTF("/api/mode failed: invalid mode=\"%s\"\n", mode.c_str());
    responder_.sendJson(400, "{\"error\":\"Invalid mode\"}");
    return;
  }

  ClockConfig cfg = configManager.loadClockConfig();
  cfg.activeMode = nextMode;
  configManager.saveClockConfig(cfg);
  clockApplySettings(configManager.sanitizeClockConfig(cfg));
  responder_.sendJson(200, "{\"message\":\"Mode changed\"}");
}

void ConfigApi::handleBrightness() {
  JsonDocument doc;
  if (!parseJsonBody(doc, "/api/brightness")) return;
  if (doc["brightness"].isNull()) {
    LOG_PRINTLN("/api/brightness failed: brightness required");
    responder_.sendJson(400, "{\"error\":\"Brightness required\"}");
    return;
  }

  clockSetBrightness(sanitizeBrightness(doc["brightness"].as<int>()));
  responder_.sendJson(200, "{\"message\":\"Brightness previewed\"}");
}

void ConfigApi::handleTime() {
  const DateTime dt = rtcGetNow();
  char buf[96];
  snprintf(buf, sizeof(buf),
           "{\"date\":\"%04d-%02d-%02d\",\"time\":\"%02d:%02d:%02d\",\"dateTime\":\"%04d-%02d-%02d %02d:%02d:%02d\"}",
           dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second(),
           dt.year(), dt.month(), dt.day(), dt.hour(), dt.minute(), dt.second());
  responder_.sendJson(200, buf);
}

void ConfigApi::handleTimeSync() {
  JsonDocument doc;
  if (!parseJsonBody(doc, "/api/time")) return;

  const int year = doc["year"] | 0;
  const int month = doc["month"] | 0;
  const int day = doc["day"] | 0;
  const int hour = doc["hour"] | 0;
  const int minute = doc["minute"] | 0;
  const int second = doc["second"] | 0;
  if (year < 2020 || year > 2099 || month < 1 || month > 12 ||
      day < 1 || day > 31 || hour < 0 || hour > 23 ||
      minute < 0 || minute > 59 || second < 0 || second > 59) {
    LOG_PRINTF("/api/time failed: invalid time %04d-%02d-%02d %02d:%02d:%02d\n",
               year, month, day, hour, minute, second);
    responder_.sendJson(400, "{\"error\":\"Invalid time\"}");
    return;
  }

  LOG_PRINTF("Browser time sync requested: %04d-%02d-%02d %02d:%02d:%02d\n",
             year, month, day, hour, minute, second);
  rtcSetNow(DateTime(year, month, day, hour, minute, second));
  responder_.sendJson(200, "{\"message\":\"RTC synced\"}");
}

void ConfigApi::handleFormats() {
  JsonDocument doc;
  JsonArray countdown = doc["countdown"].to<JsonArray>();
  for (uint8_t i = 0; i < formatCount(kFmtGroupCountdown); ++i) {
    countdown.add(getFormat(kFmtGroupCountdown, i));
  }
  JsonArray countup = doc["countup"].to<JsonArray>();
  for (uint8_t i = 0; i < formatCount(kFmtGroupCountUp); ++i) {
    countup.add(getFormat(kFmtGroupCountUp, i));
  }
  JsonArray clock = doc["clock"].to<JsonArray>();
  for (uint8_t i = 0; i < formatCount(kFmtGroupClock); ++i) {
    clock.add(getFormat(kFmtGroupClock, i));
  }
  responder_.sendJsonDocument(200, doc);
}

void ConfigApi::handleGetConfig() {
  JsonDocument doc;
  populateConfigJson(doc);
  responder_.sendJsonDocument(200, doc);
}

void ConfigApi::handleSaveConfig() {
  JsonDocument doc;
  if (!parseJsonBody(doc, "/api/config")) return;

  ClockConfig clockConfig = configManager.loadClockConfig();
  JsonVariant display     = doc["display"];
  JsonVariant modes       = display["modes"];
  JsonVariant messages    = display["messages"];
  JsonVariant time        = doc["time"];
  JsonVariant location    = doc["location"];
  JsonVariant sunset      = doc["sunset"];
  JsonVariant wifi        = doc["wifi"];
  JsonVariant station     = wifi["station"];
  JsonVariant accessPoint = wifi["accessPoint"];

  if (!applyModeAndFormats(display, modes, clockConfig)) {
    responder_.sendJson(400, "{\"error\":\"Invalid active mode\"}");
    return;
  }
  applyMessageFields(messages, clockConfig);

  if (!applyLocationFields(location, clockConfig)) {
    responder_.sendJson(400, "{\"error\":\"ZIP code must be 5 digits\"}");
    return;
  }

  if (!applySunsetFields(sunset, clockConfig)) {
    responder_.sendJson(400, "{\"error\":\"ZIP code must be 5 digits\"}");
    return;
  }

  applyTimezoneFields(time, clockConfig);

  configManager.saveClockConfig(clockConfig);
  clockApplySettings(configManager.sanitizeClockConfig(clockConfig));

  bool wifiChanged = false;
  if (!station["ssid"].isNull() || !station["password"].isNull() ||
      !accessPoint["ssid"].isNull() || !accessPoint["password"].isNull()) {
    WifiConfig wifiConfig = configManager.loadWifiConfig();
    if (!station["ssid"].isNull())      wifiConfig.staSsid     = station["ssid"].as<String>();
    if (!station["password"].isNull())  wifiConfig.staPassword = station["password"].as<String>();
    if (!accessPoint["ssid"].isNull())  wifiConfig.apSsid      = accessPoint["ssid"].as<String>();
    if (!accessPoint["password"].isNull()) wifiConfig.apPassword = accessPoint["password"].as<String>();
    configManager.saveWifiConfig(wifiConfig);
    wifiChanged = true;
  }

  if (wifiChanged) {
    responder_.sendJson(200, "{\"message\":\"Saved \xe2\x80\x94 rebooting\xe2\x80\xa6\",\"reboot\":true}");
    rebootScheduler_.scheduleReboot(kRebootDelayMs);
  } else {
    responder_.sendJson(200, "{\"message\":\"Saved\"}");
  }
}

void ConfigApi::handleSunset() {
  JsonDocument doc;
  if (!parseJsonBody(doc, "/api/sunset")) return;

  const float latitude = doc["location"]["latitude"] | NAN;
  const float longitude = doc["location"]["longitude"] | NAN;
  const char* dateTextArg = doc["time"]["date"] | "";
  const char* timeTextArg = doc["time"]["time"] | "";
  LOG_PRINTF("/api/sunset request: raw date=\"%s\" raw time=\"%s\" lat=%.6f lon=%.6f offset=%d dst=%s\n",
             dateTextArg,
             timeTextArg,
             latitude,
             longitude,
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

  const Location location{latitude, longitude, static_cast<int16_t>(utcOffsetMinutes)};
  const DateTime calculatorDate(year, month, day, 0, 0, 0);
  LOG_PRINTF("Sunset calculator args: localDate=%04d-%02d-%02d latitude=%.6f longitude=%.6f utcOffsetMinutes=%d\n",
             calculatorDate.year(), calculatorDate.month(), calculatorDate.day(),
             location.latitude, location.longitude, location.utcOffsetMinutes);
  const DateTime sunset = calculateSunset(calculatorDate, location);
  LOG_PRINTF("Sunset calculator response: localSunset=%04d-%02d-%02d %02d:%02d:%02d\n",
             sunset.year(), sunset.month(), sunset.day(),
             sunset.hour(), sunset.minute(), sunset.second());

  LOG_PRINTF("/api/sunset success: lat=%.6f lon=%.6f date=%04d-%02d-%02d offset=%d dst=%s sunset=%02d:%02d:%02d\n",
             latitude, longitude, year, month, day,
             utcOffsetMinutes, dst ? "true" : "false",
             sunset.hour(), sunset.minute(), sunset.second());

  char dateText[16], timeText[12], dateTimeText[28];
  snprintf(dateText, sizeof(dateText), "%04d-%02d-%02d",
           sunset.year(), sunset.month(), sunset.day());
  snprintf(timeText, sizeof(timeText), "%02d:%02d:%02d",
           sunset.hour(), sunset.minute(), sunset.second());
  snprintf(dateTimeText, sizeof(dateTimeText), "%s %s", dateText, timeText);

  JsonDocument response;
  response["date"]             = dateText;
  response["time"]             = timeText;
  response["dateTime"]         = dateTimeText;
  response["timezone"]         = timezone;
  response["utcOffsetMinutes"] = utcOffsetMinutes;
  response["dst"]              = dst;
  responder_.sendJsonDocument(200, response);
}

void ConfigApi::handleZipcodeLookup() {
  const String zipcode = server_.arg("zip");
  LOG_PRINTF("/api/zipcode/lookup requested: zip=\"%s\"\n", zipcode.c_str());
  if (!isValidZipcode(zipcode.c_str())) {
    LOG_PRINTF("/api/zipcode/lookup failed: invalid zipcode=\"%s\"\n", zipcode.c_str());
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

void ConfigApi::handleFieldMismatch() {
  JsonDocument doc;
  if (!parseJsonBody(doc, "/api/field-mismatch")) return;

  char page[32], field[32], configValue[80], acceptedValue[80], reason[80];
  sanitizePrintableText(doc["page"]          | "", page,          sizeof(page));
  sanitizePrintableText(doc["field"]         | "", field,         sizeof(field));
  sanitizePrintableText(doc["configValue"]   | "", configValue,   sizeof(configValue));
  sanitizePrintableText(doc["acceptedValue"] | "", acceptedValue, sizeof(acceptedValue));
  sanitizePrintableText(doc["reason"]        | "", reason,        sizeof(reason));

  LOG_PRINTF("FIELD MISMATCH page=\"%s\" field=\"%s\" config=\"%s\" accepted=\"%s\" reason=\"%s\"\n",
             page, field, configValue, acceptedValue, reason);
  responder_.sendJson(200, "{\"message\":\"logged\"}");
}

void ConfigApi::populateConfigJson(JsonDocument& doc) {
  const ClockConfig clockConfig = configManager.loadClockConfig();
  const WifiConfig  wifiConfig  = configManager.loadWifiConfig();
  logConfigResponse(clockConfig, wifiConfig);

  serializeClockConfig(doc, clockConfig);
  serializeWifiStatus(doc, wifiConfig);
}

void ConfigApi::logConfigResponse(const ClockConfig& clockConfig,
                                  const WifiConfig& wifiConfig) const {
  LOG_PRINTF("/api/config response: mode=%u countdownFmt=%u countupFmt=%u clockFmt=%u brightness=%u\n",
             static_cast<unsigned>(clockConfig.activeMode),
             clockConfig.countdownFmt,
             clockConfig.countupFmt,
             clockConfig.clockFmt,
             clockConfig.brightness);
  LOG_PRINTF("/api/config response: countdownDatetime=\"%s\" countupDatetime=\"%s\"\n",
             clockConfig.countdownDatetime,
             clockConfig.countupDatetime);
  LOG_PRINTF("/api/config response: splashMessage=\"%s\" finalMessage=\"%s\"\n",
             clockConfig.splashMessage,
             clockConfig.finalMessage);
  LOG_PRINTF("/api/config response: latitude=%.6f longitude=%.6f zipcode=\"%s\" sunsetLatitude=%.6f sunsetLongitude=%.6f sunsetZipcode=\"%s\" timezone=\"%s\" utcOffsetMinutes=%d dst=%s\n",
             clockConfig.location.latitude,
             clockConfig.location.longitude,
             clockConfig.location.zipcode,
             clockConfig.sunsetTest.latitude,
             clockConfig.sunsetTest.longitude,
             clockConfig.sunsetTest.zipcode,
             clockConfig.timezone,
             clockConfig.utcOffsetMinutes,
             clockConfig.dst ? "true" : "false");
  LOG_PRINTF("/api/config response: staSsid=\"%s\" apSsid=\"%s\" apPassword=<%u chars>\n",
             wifiConfig.staSsid.c_str(),
             wifiConfig.apSsid.c_str(),
             wifiConfig.apPassword.length());
}
