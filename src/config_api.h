#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>

#include "http_responder.h"
#include "reboot_scheduler.h"

struct ClockConfig;
struct WifiConfig;
class ClockController;
class ConfigManager;
class RtcService;
class BeepPlayer;

// Handles clock-configuration HTTP endpoints by validating input and invoking application actions.
class ConfigApi {
 public:
  ConfigApi(ESP8266WebServer& server, HttpResponder& responder,
            ClockController& clockController,
            ConfigManager& configManager,
            BeepPlayer& beepPlayer,
            RtcService& rtc,
            RebootScheduler& rebootScheduler)
      : m_server(server),
        m_responder(responder),
        m_clockController(clockController),
        m_configManager(configManager),
        m_sound(beepPlayer),
        m_rtc(rtc),
        m_rebootScheduler(rebootScheduler) {}

  void handleDemoTest();
  void handleMessageTest();
  void handleSetMode();
  void handleBrightness();
  void handleFormats();
  void handleSoundTest();
  void handleGetConfig();
  void handleSaveConfig();
  void handleFieldMismatch();

 private:
  // Stamps an absolute datetime over the kCountupStartNow sentinel. Reads the
  // clock itself rather than taking a DateTime, so no caller can hand it a time
  // the RTC does not vouch for.
  // A no-op, logged, when RtcService::timeIsTrustworthy() is false.
  bool resolveCountupStart(ClockConfig& config);

  // The only two ways this class writes a ClockConfig to disk. Both resolve the
  // count-up origin first, so a save path added later cannot skip it; that is
  // the whole reason they exist rather than calling ConfigManager directly.
  bool persistClockConfig(ClockConfig& config);
  bool persistClockConfig(ClockConfig& config, const WifiConfig& wifi);

  void populateConfigJson(JsonDocument& doc);
  // Mirrors a /api/config response body to the serial monitor. Call it after
  // the response has been sent: writing ~1KB at 74880 baud blocks for roughly
  // 150ms, which must not sit on the browser's wait.
  void logConfigJson(const JsonDocument& doc) const;

  ESP8266WebServer& m_server;       // Source of request payloads and query args.
  HttpResponder& m_responder;       // Sends JSON/HTML API responses.
  ClockController& m_clockController;  // Executes application-level clock actions.
  ConfigManager& m_configManager;      // Loads, validates, and saves configuration.
  BeepPlayer& m_sound;  // Generated-beep previews for /sound.
  RtcService& m_rtc;  // Stamps the count-up origin when a save resolves "now".
  RebootScheduler& m_rebootScheduler;  // Schedules a reboot after WiFi changes.
};
