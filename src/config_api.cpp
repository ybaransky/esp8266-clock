#include "config_api.h"

#include <ArduinoJson.h>
#include <string.h>

#include "datetime_validation.h"
#include "display_format.h"
#include "clock_controller.h"
#include "config.h"
#include "config_serializer.h"
#include "config_validation.h"
#include "log.h"
#include "rtc_ds3231.h"
#include "beep_player.h"


namespace {

constexpr uint32_t kRebootDelayMs = 1500;  // Lets the HTTP response flush before restarting.

}  // namespace

// -----------------------------------------------------------------------------
// ConfigApi
// -----------------------------------------------------------------------------

// The field this writes must hold the canonical form in full: a buffer one byte
// short would truncate the seconds, and truncated text no longer parses, so the
// origin would silently revert to the default on the next load.
static_assert(sizeof(CountupConfig::start) >= kLocalDateTimeLength,
              "CountupConfig::start cannot hold a canonical datetime");

bool ConfigApi::resolveCountupStart(ClockConfig& config) {
  if (strcmp(config.countup.start, kCountupStartNow) != 0) return false;
  // Leave an unset origin unset rather than persist a time the clock does not
  // vouch for. getNowCached()'s fallback exists for fault presentation, and
  // lost-power recovery leaves the chip holding the firmware build date - both
  // are wrong things to write into /config.json, where they would outlive the
  // fault and never self-correct.
  if (!m_rtc.timeIsTrustworthy()) {
    LOG_PRINTLN("countup start left unset: no trustworthy RTC time to stamp");
    return false;
  }
  formatLocalDateTime(m_rtc.getNowCached(), config.countup.start,
                      sizeof(config.countup.start));
  LOG_PRINTF("countup start resolved from \"now\" to %s", config.countup.start);
  return true;
}

bool ConfigApi::persistClockConfig(ClockConfig& config) {
  resolveCountupStart(config);
  return m_configManager.saveClockConfig(config);
}

bool ConfigApi::persistClockConfig(ClockConfig& config, const WifiConfig& wifi) {
  resolveCountupStart(config);
  return m_configManager.saveConfig(config, wifi);
}

void ConfigApi::handleDemoTest() {
  if (m_server.hasArg("plain") && (m_server.arg("plain").length() > 0)) {
    JsonDocument doc;
    if (!m_responder.parseJsonBody(doc, "/api/demo/test")) return;
    JsonVariant finalMessage = doc["display"]["messages"]["final"];
    if (!finalMessage.isNull()) {
      ClockConfig config = m_configManager.clockConfig();
      sanitizeDisplayMessage(finalMessage.as<const char*>(),
                             config.messages.countdownDone,
                             sizeof(config.messages.countdownDone));
      m_clockController.applyConfig(config);
    }
  }

  m_clockController.showDemo();
  m_responder.sendJson(200, "{\"preview_ms\":10000}");
}

void ConfigApi::handleMessageTest() {
  JsonDocument doc;
  if (!m_responder.parseJsonBody(doc, "/api/message/test")) return;

  char message[kDisplayMessageLength];
  sanitizeDisplayMessage(doc["message"] | "", message, sizeof(message));
  if (doc["blink"] | false) {
    // Preview with the same blinking treatment the message gets for real
    // (e.g. the Friday sunset message).
    m_clockController.showInfo(message, 5000);
  } else {
    m_clockController.showSplash(message);
  }
  m_responder.sendJson(200, "{\"message\":\"Previewing message\",\"preview_ms\":5000}");
}

void ConfigApi::handleSetMode() {
  JsonDocument doc;
  if (!m_responder.parseJsonBody(doc, "/api/mode")) return;

  Mode nextMode;
  const String mode = doc["mode"] | "";
  if (!modeFromName(mode, &nextMode)) {
    LOG_PRINTF("/api/mode failed: invalid mode=\"%s\"", mode.c_str());
    m_responder.sendJsonError(400, "Invalid mode");
    return;
  }

  ClockConfig config = m_configManager.clockConfig();
  config.activeMode = nextMode;
  if (!persistClockConfig(config)) {
    LOG_PRINTLN("/api/mode failed: complete config write failed");
    m_responder.sendJsonError(500, "Configuration write failed");
    return;
  }
  m_clockController.applyConfig(config);
  m_responder.sendJson(200, "{\"message\":\"Mode changed\"}");
}

void ConfigApi::handleBrightness() {
  JsonDocument doc;
  if (!m_responder.parseJsonBody(doc, "/api/brightness")) return;
  if (doc["brightness"].isNull()) {
    LOG_PRINTLN("/api/brightness failed: brightness required");
    m_responder.sendJsonError(400, "Brightness required");
    return;
  }

  m_clockController.setBrightness(sanitizeBrightness(doc["brightness"].as<int>()));
  m_responder.sendJson(200, "{\"message\":\"Brightness previewed\"}");
}

// Each entry is {key, label}: the key is what the page posts back and what
// config.json stores, the label is only ever shown. Sending the index would
// make the dropdown's value depend on this firmware's table order, which is
// exactly what a stored key exists to avoid.
void ConfigApi::handleFormats() {
  JsonDocument doc;
  const struct {
    const char* jsonKey;  // Response property naming the group.
    FormatGroup group;    // Catalog listed under that property.
  } kGroups[] = {
      {"countdown", kFmtGroupCountdown},
      {"countup", kFmtGroupCountUp},
      {"clock", kFmtGroupClock},
  };
  for (const auto& group : kGroups) {
    JsonArray formats = doc[group.jsonKey].to<JsonArray>();
    for (uint8_t i = 0; i < displayFormatCount(group.group); ++i) {
      // Keys and labels live in flash, so they are copied into these buffers
      // before ArduinoJson duplicates them into the response document.
      char key[kFormatKeyLength];
      char label[kFormatLabelLength];
      displayFormatKey(group.group, i, key, sizeof(key));
      displayFormatLabel(group.group, i, label, sizeof(label));
      JsonObject entry = formats.add<JsonObject>();
      entry["key"] = key;
      entry["label"] = label;
    }
  }
  m_responder.sendJsonDocument(200, doc);
}

void ConfigApi::handleSoundTest() {
  JsonDocument doc;
  if (!m_responder.parseJsonBody(doc, "/api/sound/test")) return;

  if (doc["stop"] | false) {
    m_sound.stop();
    m_responder.sendJson(200, "{\"message\":\"Stopped\"}");
    return;
  }

  JsonVariantConst boundaryAlert = doc["boundaryAlert"];
  if (boundaryAlert["frequencyHz"].isNull() ||
      boundaryAlert["totalDurationSeconds"].isNull() ||
      boundaryAlert["startingBeatsHz"].isNull()) {
    m_responder.sendJsonError(400, "Tone, total duration, and starting beats required");
    return;
  }
  const BeepPattern pattern{
      sanitizeBoundaryFrequencyHz(boundaryAlert["frequencyHz"].as<int>()),
      sanitizeBoundaryDurationSeconds(boundaryAlert["totalDurationSeconds"].as<int>()),
      sanitizeBoundaryStartingBeatsHz(boundaryAlert["startingBeatsHz"].as<int>())};
  // An explicit preview bypasses the automatic-sound master switch.
  m_sound.previewBoundaryAlert(pattern, millis());
  JsonDocument response;
  response["message"] = "Playing boundary alert";
  response["durationMs"] = static_cast<uint32_t>(pattern.totalDurationSeconds) * 1000U;
  m_responder.sendJsonDocument(200, response);
}

void ConfigApi::handleGetConfig() {
  JsonDocument doc;
  populateConfigJson(doc);
  m_responder.sendJsonDocument(200, doc);
  logConfigJson(doc);
}

void ConfigApi::handleSaveConfig() {
  JsonDocument doc;
  if (!m_responder.parseJsonBody(doc, "/api/config")) return;
  JsonVariantConst payload = doc.as<JsonVariantConst>();

  ClockConfig clockConfig = m_configManager.clockConfig();
  const char* error = applyJsonToClockConfig(payload, clockConfig);
  if (error != nullptr) {
    LOG_PRINTF("/api/config rejected clock settings: %s", error);
    m_responder.sendJson(400, error);
    return;
  }
  WifiConfig wifiConfig = m_configManager.wifiConfig();
  const bool wifiChanged = applyJsonToWifiConfig(payload, wifiConfig);
  if (!persistClockConfig(clockConfig, wifiConfig)) {
    LOG_PRINTLN("/api/config failed: complete config write failed");
    m_responder.sendJsonError(500, "Configuration write failed");
    return;
  }
  m_clockController.applyConfig(clockConfig);

  if (wifiChanged) {
    m_responder.sendJson(200, "{\"message\":\"Saved \xe2\x80\x94 rebooting\xe2\x80\xa6\",\"reboot\":true}");
    m_rebootScheduler.scheduleReboot(kRebootDelayMs);
  } else {
    // Return canonical values so forms can retain server-resolved datetimes.
    // Reuse the request document after all payload readers have finished.
    doc.clear();
    serializeClockConfig(doc, clockConfig);
    doc["message"] = "Saved";
    m_responder.sendJsonDocument(200, doc);
  }
}

void ConfigApi::handleFieldMismatch() {
  JsonDocument doc;
  if (!m_responder.parseJsonBody(doc, "/api/field-mismatch")) return;

  char page[32], field[32], configValue[80], acceptedValue[80], reason[80];
  sanitizePrintableText(doc["page"]          | "", page,          sizeof(page));
  sanitizePrintableText(doc["field"]         | "", field,         sizeof(field));
  sanitizePrintableText(doc["configValue"]   | "", configValue,   sizeof(configValue));
  sanitizePrintableText(doc["acceptedValue"] | "", acceptedValue, sizeof(acceptedValue));
  sanitizePrintableText(doc["reason"]        | "", reason,        sizeof(reason));

  LOG_PRINTF("FIELD MISMATCH page=\"%s\" field=\"%s\" config=\"%s\" accepted=\"%s\" reason=\"%s\"",
             page, field, configValue, acceptedValue, reason);
  m_responder.sendJson(200, "{\"message\":\"logged\"}");
}

void ConfigApi::populateConfigJson(JsonDocument& doc) {
  const ClockConfig& clockConfig = m_configManager.clockConfig();
  const WifiConfig&  wifiConfig  = m_configManager.wifiConfig();
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
