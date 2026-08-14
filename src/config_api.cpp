#include "config_api.h"

#include <ArduinoJson.h>

#include "display_format.h"
#include "clock_controller.h"
#include "config.h"
#include "config_serializer.h"
#include "config_validation.h"
#include "log.h"
#include "sound_player.h"  // SoundKind
#include "web_server.h"

namespace {

constexpr uint32_t kRebootDelayMs = 1500;

}  // namespace

// -----------------------------------------------------------------------------
// ConfigApi
// -----------------------------------------------------------------------------

void ConfigApi::handleDemoTest() {
  if (server_.hasArg("plain") && (server_.arg("plain").length() > 0)) {
    JsonDocument doc;
    if (!responder_.parseJsonBody(doc, "/api/demo/test")) return;
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
  if (!responder_.parseJsonBody(doc, "/api/message/test")) return;

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
  if (!responder_.parseJsonBody(doc, "/api/mode")) return;

  Mode nextMode;
  const String mode = doc["mode"] | "";
  if (!modeFromName(mode, &nextMode)) {
    LOG_PRINTF("/api/mode failed: invalid mode=\"%s\"", mode.c_str());
    responder_.sendJsonError(400, "Invalid mode");
    return;
  }

  ClockConfig cfg = configManager_.loadClockConfig();
  cfg.activeMode = nextMode;
  if (!configManager_.saveClockConfig(cfg)) {
    LOG_PRINTLN("/api/mode failed: complete config write failed");
    responder_.sendJsonError(500, "Configuration write failed");
    return;
  }
  clockController_.applyConfig(cfg);
  responder_.sendJson(200, "{\"message\":\"Mode changed\"}");
}

void ConfigApi::handleBrightness() {
  JsonDocument doc;
  if (!responder_.parseJsonBody(doc, "/api/brightness")) return;
  if (doc["brightness"].isNull()) {
    LOG_PRINTLN("/api/brightness failed: brightness required");
    responder_.sendJsonError(400, "Brightness required");
    return;
  }

  clockController_.setBrightness(sanitizeBrightness(doc["brightness"].as<int>()));
  responder_.sendJson(200, "{\"message\":\"Brightness previewed\"}");
}

void ConfigApi::handleFormats() {
  JsonDocument doc;
  JsonArray countdown = doc["countdown"].to<JsonArray>();
  for (uint8_t i = 0; i < displayFormatCount(kFmtGroupCountdown); ++i) {
    countdown.add(displayFormatInfo(kFmtGroupCountdown, i).label);
  }
  JsonArray countup = doc["countup"].to<JsonArray>();
  for (uint8_t i = 0; i < displayFormatCount(kFmtGroupCountUp); ++i) {
    countup.add(displayFormatInfo(kFmtGroupCountUp, i).label);
  }
  JsonArray clock = doc["clock"].to<JsonArray>();
  for (uint8_t i = 0; i < displayFormatCount(kFmtGroupClock); ++i) {
    clock.add(displayFormatInfo(kFmtGroupClock, i).label);
  }
  responder_.sendJsonDocument(200, doc);
}

// Songs and alerts are returned as separate arrays rather than one list with a
// kind on each entry: every consumer wants exactly one of the two to fill a
// dropdown, so splitting here keeps that filter out of each page's script.
void ConfigApi::handleSounds() {
  JsonDocument doc;
  JsonArray songs = doc["songs"].to<JsonArray>();
  JsonArray alerts = doc["alerts"].to<JsonArray>();
  const bool songsRead =
      clockController_.soundNamesAsJson(songs, SoundKind::kSong);
  const bool alertsRead =
      clockController_.soundNamesAsJson(alerts, SoundKind::kAlert);
  // Reported separately from an empty array: a device with no /songs.bin is
  // missing its filesystem image, which is worth saying out loud on the page
  // rather than showing as a dropdown that happens to have no entries.
  doc["available"] = (songsRead && alertsRead);
  responder_.sendJsonDocument(200, doc);
}

void ConfigApi::handleSoundTest() {
  JsonDocument doc;
  if (!responder_.parseJsonBody(doc, "/api/sound/test")) return;

  if (doc["stop"] | false) {
    clockController_.stopSound();
    responder_.sendJson(200, "{\"message\":\"Stopped\"}");
    return;
  }

  char name[kSoundNameLength];
  sanitizePrintableText(doc["sound"] | "", name, sizeof(name));
  if (name[0] == '\0') {
    responder_.sendJsonError(400, "Sound name required");
    return;
  }
  // Deliberately bypasses the master sound switch: pressing preview is an
  // explicit request to hear something, and staying silent would read as a
  // broken button rather than as a setting.
  if (!clockController_.playSound(name)) {
    LOG_PRINTF("/api/sound/test failed: no sound named \"%s\"", name);
    responder_.sendJsonError(404, "No such sound");
    return;
  }
  // The play/stop button reverts itself when this elapses. Sent with the start
  // so the page never has to poll a device that serves one connection at a
  // time; it is measured after playback begins, off the browser's critical
  // path for hearing the sound.
  JsonDocument response;
  response["message"] = "Playing";
  response["durationMs"] = clockController_.soundDurationMs(name);
  responder_.sendJsonDocument(200, response);
}

void ConfigApi::handleGetConfig() {
  JsonDocument doc;
  populateConfigJson(doc);
  responder_.sendJsonDocument(200, doc);
  logConfigJson(doc);
}

void ConfigApi::handleSaveConfig() {
  JsonDocument doc;
  if (!responder_.parseJsonBody(doc, "/api/config")) return;
  JsonVariantConst payload = doc.as<JsonVariantConst>();

  ClockConfig clockConfig = configManager_.loadClockConfig();
  const char* error = applyJsonToClockConfig(payload, clockConfig);
  if (error != nullptr) {
    LOG_PRINTF("/api/config rejected clock settings: %s", error);
    responder_.sendJson(400, error);
    return;
  }
  WifiConfig wifiConfig = configManager_.loadWifiConfig();
  const bool wifiChanged = applyJsonToWifiConfig(payload, wifiConfig);
  if (!configManager_.saveConfig(clockConfig, wifiConfig)) {
    LOG_PRINTLN("/api/config failed: complete config write failed");
    responder_.sendJsonError(500, "Configuration write failed");
    return;
  }
  clockController_.applyConfig(clockConfig);

  if (wifiChanged) {
    responder_.sendJson(200, "{\"message\":\"Saved \xe2\x80\x94 rebooting\xe2\x80\xa6\",\"reboot\":true}");
    webPortal_.scheduleReboot(kRebootDelayMs);
  } else {
    responder_.sendJson(200, "{\"message\":\"Saved\"}");
  }
}

void ConfigApi::handleFieldMismatch() {
  JsonDocument doc;
  if (!responder_.parseJsonBody(doc, "/api/field-mismatch")) return;

  char page[32], field[32], configValue[80], acceptedValue[80], reason[80];
  sanitizePrintableText(doc["page"]          | "", page,          sizeof(page));
  sanitizePrintableText(doc["field"]         | "", field,         sizeof(field));
  sanitizePrintableText(doc["configValue"]   | "", configValue,   sizeof(configValue));
  sanitizePrintableText(doc["acceptedValue"] | "", acceptedValue, sizeof(acceptedValue));
  sanitizePrintableText(doc["reason"]        | "", reason,        sizeof(reason));

  LOG_PRINTF("FIELD MISMATCH page=\"%s\" field=\"%s\" config=\"%s\" accepted=\"%s\" reason=\"%s\"",
             page, field, configValue, acceptedValue, reason);
  responder_.sendJson(200, "{\"message\":\"logged\"}");
}

void ConfigApi::populateConfigJson(JsonDocument& doc) {
  const ClockConfig clockConfig = configManager_.loadClockConfig();
  const WifiConfig  wifiConfig  = configManager_.loadWifiConfig();
  LOG_PRINTF("/api/config response: mode=%s brightness=%u staSsid=\"%s\"",
             modeName(clockConfig.activeMode),
             clockConfig.display.brightness,
             wifiConfig.staSsid.c_str());

  serializeClockConfig(doc, clockConfig);
  serializeWifiStatus(doc, wifiConfig);
}

void ConfigApi::logConfigJson(const JsonDocument& doc) const {
  // Streamed straight to Serial instead of through LOG_PRINTF: the body is
  // about a kilobyte, and buffering it into RAM just to hand it to a "%s"
  // would cost more than the ESP8266 has to spare on a web handler. The
  // LOG_PRINTLN above carries the usual time/stack/source prefix.
  //
  // Indented rather than sent verbatim: this line exists to be read on the
  // console, and the browser got the compact bytes either way. The whitespace
  // roughly doubles the serial time (~150ms to ~300ms at 74880 baud), which is
  // affordable only because this runs after the response has been sent.
  LOG_PRINTLN("/api/config body:");
  serializeJsonPretty(doc, Serial);
  Serial.println();
}
