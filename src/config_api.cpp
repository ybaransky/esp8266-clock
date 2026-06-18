#include "config_api.h"

#include <ArduinoJson.h>

#include "clock_state.h"
#include "config.h"
#include "config_validation.h"
#include "format.h"
#include "log.h"
#include "rtc_ds3231.h"
#include "zipcode.h"

namespace {

constexpr uint32_t kRebootDelayMs = 1500;

}  // namespace

void ConfigApi::handleDemoTest() {
  if (server_.hasArg("plain") && server_.arg("plain").length() > 0) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server_.arg("plain"));
    if (err) {
      LOG_PRINTF("/api/demo/test failed: invalid JSON: %s\n", err.c_str());
      responder_.sendJson(400, "{\"error\":\"Invalid JSON\"}");
      return;
    }
    if (!doc["finalMessage"].isNull()) {
      ClockConfig cfg = configManager.loadClockConfig();
      sanitizeDisplayMessage(doc["finalMessage"].as<const char*>(),
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
  DeserializationError err = deserializeJson(doc, server_.arg("plain"));
  if (err) {
    LOG_PRINTF("/api/message/test failed: invalid JSON: %s\n", err.c_str());
    responder_.sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }

  char message[64];
  sanitizeDisplayMessage(doc["message"] | "", message, sizeof(message));
  clockShowMessagePreview(message);
  responder_.sendJson(200, "{\"message\":\"Previewing message\",\"preview_ms\":5000}");
}

void ConfigApi::handleSetMode() {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server_.arg("plain"));
  if (err) {
    LOG_PRINTF("/api/mode failed: invalid JSON: %s\n", err.c_str());
    responder_.sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }

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
  DeserializationError err = deserializeJson(doc, server_.arg("plain"));
  if (err) {
    LOG_PRINTF("/api/brightness failed: invalid JSON: %s\n", err.c_str());
    responder_.sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }
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
  DeserializationError err = deserializeJson(doc, server_.arg("plain"));
  if (err) {
    LOG_PRINTF("/api/time/sync failed: invalid JSON: %s\n", err.c_str());
    responder_.sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }

  const int year = doc["year"] | 0;
  const int month = doc["month"] | 0;
  const int day = doc["day"] | 0;
  const int hour = doc["hour"] | 0;
  const int minute = doc["minute"] | 0;
  const int second = doc["second"] | 0;
  if (year < 2020 || year > 2099 || month < 1 || month > 12 ||
      day < 1 || day > 31 || hour < 0 || hour > 23 ||
      minute < 0 || minute > 59 || second < 0 || second > 59) {
    LOG_PRINTF("/api/time/sync failed: invalid time %04d-%02d-%02d %02d:%02d:%02d\n",
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
  sendJsonDocument(doc);
}

void ConfigApi::handleGetConfig() {
  JsonDocument doc;
  populateConfigJson(doc);
  sendJsonDocument(doc);
}

void ConfigApi::handleSaveConfig() {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server_.arg("plain"));
  if (err) {
    LOG_PRINTF("/api/config save failed: invalid JSON: %s\n", err.c_str());
    responder_.sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }

  ClockConfig clockConfig = configManager.loadClockConfig();
  if (!doc["mode"].isNull()) {
    clockConfig.activeMode = sanitizePersistentMode(doc["mode"].as<int>(),
                                                    clockConfig.activeMode);
  }
  if (!doc["countdownFmt"].isNull()) {
    clockConfig.countdownFmt = sanitizeFormatIndex(kFmtGroupCountdown,
                                                   doc["countdownFmt"].as<int>(),
                                                   clockConfig.countdownFmt);
  }
  if (!doc["countupFmt"].isNull()) {
    clockConfig.countupFmt = sanitizeFormatIndex(kFmtGroupCountUp,
                                                 doc["countupFmt"].as<int>(),
                                                 clockConfig.countupFmt);
  }
  if (!doc["clockFmt"].isNull()) {
    clockConfig.clockFmt = sanitizeFormatIndex(kFmtGroupClock,
                                               doc["clockFmt"].as<int>(),
                                               clockConfig.clockFmt);
  }
  if (!doc["brightness"].isNull()) {
    clockConfig.brightness = sanitizeBrightness(doc["brightness"].as<int>());
  }
  if (!doc["countdownDatetime"].isNull()) {
    snprintf(clockConfig.countdownDatetime, sizeof(clockConfig.countdownDatetime),
             "%s", doc["countdownDatetime"].as<const char*>());
  }
  if (!doc["countupDatetime"].isNull()) {
    snprintf(clockConfig.countupDatetime, sizeof(clockConfig.countupDatetime),
             "%s", doc["countupDatetime"].as<const char*>());
  }
  if (!doc["splashMessage"].isNull()) {
    sanitizeDisplayMessage(doc["splashMessage"].as<const char*>(),
                           clockConfig.splashMessage,
                           sizeof(clockConfig.splashMessage));
  }
  if (!doc["finalMessage"].isNull()) {
    sanitizeDisplayMessage(doc["finalMessage"].as<const char*>(),
                           clockConfig.finalMessage,
                           sizeof(clockConfig.finalMessage));
  }
  if (!doc["latitude"].isNull()) {
    clockConfig.latitude = doc["latitude"].as<float>();
  }
  if (!doc["longitude"].isNull()) {
    clockConfig.longitude = doc["longitude"].as<float>();
  }
  if (!doc["zipcode"].isNull()) {
    const char* zipcode = doc["zipcode"].as<const char*>();
    if (zipcode == nullptr || (zipcode[0] != '\0' && !isValidZipcode(zipcode))) {
      LOG_PRINTF("/api/config save failed: invalid zipcode=\"%s\"\n",
                 zipcode == nullptr ? "(null)" : zipcode);
      responder_.sendJson(400, "{\"error\":\"ZIP code must be 5 digits\"}");
      return;
    }
    snprintf(clockConfig.zipcode, sizeof(clockConfig.zipcode), "%s", zipcode);
  }
  if (!doc["timezone"].isNull()) {
    sanitizePrintableText(doc["timezone"].as<const char*>(),
                          clockConfig.timezone,
                          sizeof(clockConfig.timezone));
  }
  if (!doc["utcOffsetMinutes"].isNull()) {
    clockConfig.utcOffsetMinutes =
        sanitizeUtcOffsetMinutes(doc["utcOffsetMinutes"].as<int>());
  }

  configManager.saveClockConfig(clockConfig);
  clockApplySettings(configManager.sanitizeClockConfig(clockConfig));

  bool wifiChanged = false;
  if (!doc["staSsid"].isNull() || !doc["staPassword"].isNull() ||
      !doc["apSsid"].isNull() || !doc["apPassword"].isNull()) {
    WifiConfig wifiConfig = configManager.loadWifiConfig();
    if (!doc["staSsid"].isNull()) {
      wifiConfig.staSsid = doc["staSsid"].as<String>();
    }
    if (!doc["staPassword"].isNull()) {
      wifiConfig.staPassword = doc["staPassword"].as<String>();
    }
    if (!doc["apSsid"].isNull()) {
      wifiConfig.apSsid = doc["apSsid"].as<String>();
    }
    if (!doc["apPassword"].isNull()) {
      wifiConfig.apPassword = doc["apPassword"].as<String>();
    }
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
             location.zipcode,
             location.latitude,
             location.longitude);
  char json[96];
  snprintf(json, sizeof(json),
           "{\"zipcode\":\"%s\",\"latitude\":%.6f,\"longitude\":%.6f}",
           location.zipcode, location.latitude, location.longitude);
  responder_.sendJson(200, json);
}

void ConfigApi::handleFieldMismatch() {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, server_.arg("plain"));
  if (err) {
    LOG_PRINTF("/api/field-mismatch failed: invalid JSON: %s\n", err.c_str());
    responder_.sendJson(400, "{\"error\":\"Invalid JSON\"}");
    return;
  }

  char page[32];
  char field[32];
  char configValue[80];
  char acceptedValue[80];
  char reason[80];
  sanitizePrintableText(doc["page"] | "", page, sizeof(page));
  sanitizePrintableText(doc["field"] | "", field, sizeof(field));
  sanitizePrintableText(doc["configValue"] | "", configValue, sizeof(configValue));
  sanitizePrintableText(doc["acceptedValue"] | "", acceptedValue, sizeof(acceptedValue));
  sanitizePrintableText(doc["reason"] | "", reason, sizeof(reason));

  LOG_PRINTF("FIELD MISMATCH page=\"%s\" field=\"%s\" config=\"%s\" accepted=\"%s\" reason=\"%s\"\n",
             page,
             field,
             configValue,
             acceptedValue,
             reason);
  responder_.sendJson(200, "{\"message\":\"logged\"}");
}

void ConfigApi::populateConfigJson(JsonDocument& doc) {
  const ClockConfig clockConfig = configManager.loadClockConfig();
  const WifiConfig wifiConfig = configManager.loadWifiConfig();
  logConfigResponse(clockConfig, wifiConfig);

  doc["mode"] = static_cast<int>(clockConfig.activeMode);
  doc["countdownFmt"] = clockConfig.countdownFmt;
  doc["countupFmt"] = clockConfig.countupFmt;
  doc["clockFmt"] = clockConfig.clockFmt;
  doc["brightness"] = clockConfig.brightness;
  doc["countdownDatetime"] = String(clockConfig.countdownDatetime);
  doc["countupDatetime"] = String(clockConfig.countupDatetime);
  doc["splashMessage"] = String(clockConfig.splashMessage);
  doc["finalMessage"] = String(clockConfig.finalMessage);
  doc["latitude"] = clockConfig.latitude;
  doc["longitude"] = clockConfig.longitude;
  doc["zipcode"] = String(clockConfig.zipcode);
  doc["timezone"] = String(clockConfig.timezone);
  doc["utcOffsetMinutes"] = clockConfig.utcOffsetMinutes;
  doc["staSsid"] = wifiConfig.staSsid;
  doc["apSsid"] = wifiConfig.apSsid;
  doc["apPassword"] = wifiConfig.apPassword;
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
  LOG_PRINTF("/api/config response: latitude=%.6f longitude=%.6f zipcode=\"%s\" timezone=\"%s\" utcOffsetMinutes=%d\n",
             clockConfig.latitude,
             clockConfig.longitude,
             clockConfig.zipcode,
             clockConfig.timezone,
             clockConfig.utcOffsetMinutes);
  LOG_PRINTF("/api/config response: staSsid=\"%s\" apSsid=\"%s\" apPassword=<%u chars>\n",
             wifiConfig.staSsid.c_str(),
             wifiConfig.apSsid.c_str(),
             wifiConfig.apPassword.length());
}

void ConfigApi::sendJsonDocument(JsonDocument& doc) {
  String json;
  json.reserve(measureJson(doc));
  serializeJson(doc, json);
  responder_.sendJson(200, json.c_str());
}
