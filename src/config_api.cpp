#include "config_api.h"

#include <ArduinoJson.h>

#include "clock_format.h"
#include "clock_controller.h"
#include "config.h"
#include "config_serializer.h"
#include "config_validation.h"
#include "log.h"
#include "web_server.h"

namespace {

constexpr uint32_t kRebootDelayMs = 1500;

}  // namespace

// -----------------------------------------------------------------------------
// ConfigApi
// -----------------------------------------------------------------------------

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
      ClockConfig cfg = configManager_.loadClockConfig();
      sanitizeDisplayMessage(finalMessage.as<const char*>(),
                             cfg.messages.final,
                             sizeof(cfg.messages.final));
      clockController_.applyConfig(cfg);
    }
  }

  clockController_.showDemo();
  responder_.sendJson(200, "{\"preview_ms\":10000}");
}

void ConfigApi::handleMessageTest() {
  JsonDocument doc;
  if (!parseJsonBody(doc, "/api/message/test")) return;

  char message[64];
  sanitizeDisplayMessage(doc["message"] | "", message, sizeof(message));
  if (doc["blink"] | false) {
    // Preview with the same blinking treatment the message gets for real
    // (e.g. the Friday sunset message).
    clockController_.showInfo(message, 5000);
  } else {
    clockController_.showSplash(message);
  }
  responder_.sendJson(200, "{\"message\":\"Previewing message\",\"preview_ms\":5000}");
}

void ConfigApi::handleSetMode() {
  JsonDocument doc;
  if (!parseJsonBody(doc, "/api/mode")) return;

  Mode nextMode;
  const String mode = doc["mode"] | "";
  if (!modeFromName(mode, &nextMode)) {
    LOG_PRINTF("/api/mode failed: invalid mode=\"%s\"\n", mode.c_str());
    responder_.sendJson(400, "{\"error\":\"Invalid mode\"}");
    return;
  }

  ClockConfig cfg = configManager_.loadClockConfig();
  cfg.activeMode = nextMode;
  configManager_.saveClockConfig(cfg);
  clockController_.applyConfig(configManager_.sanitizeClockConfig(cfg));
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

  clockController_.setBrightness(sanitizeBrightness(doc["brightness"].as<int>()));
  responder_.sendJson(200, "{\"message\":\"Brightness previewed\"}");
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
  JsonVariantConst payload = doc.as<JsonVariantConst>();

  ClockConfig clockConfig = configManager_.loadClockConfig();
  const char* error = applyJsonToClockConfig(payload, clockConfig);
  if (error != nullptr) {
    responder_.sendJson(400, error);
    return;
  }
  configManager_.saveClockConfig(clockConfig);
  clockController_.applyConfig(configManager_.sanitizeClockConfig(clockConfig));

  WifiConfig wifiConfig = configManager_.loadWifiConfig();
  if (applyJsonToWifiConfig(payload, wifiConfig)) {
    configManager_.saveWifiConfig(wifiConfig);
    responder_.sendJson(200, "{\"message\":\"Saved \xe2\x80\x94 rebooting\xe2\x80\xa6\",\"reboot\":true}");
    webScheduleReboot(kRebootDelayMs);
  } else {
    responder_.sendJson(200, "{\"message\":\"Saved\"}");
  }
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
  const ClockConfig clockConfig = configManager_.loadClockConfig();
  const WifiConfig  wifiConfig  = configManager_.loadWifiConfig();
  LOG_PRINTF("/api/config response: mode=%s brightness=%u staSsid=\"%s\"\n",
             modeName(clockConfig.activeMode),
             clockConfig.display.brightness,
             wifiConfig.staSsid.c_str());

  serializeClockConfig(doc, clockConfig);
  serializeWifiStatus(doc, wifiConfig);
}
