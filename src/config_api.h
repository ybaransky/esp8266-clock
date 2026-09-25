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
      : server_(server),
        responder_(responder),
        clockController_(clockController),
        configManager_(configManager),
        sound_(beepPlayer),
        rtc_(rtc),
        rebootScheduler_(rebootScheduler) {}

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
  // the RTC does not vouch for - the previous signature made that the caller's
  // problem and a missing DS3231 returned BCD garbage from an absent device.
  // A no-op, logged, when RtcService::timeIsTrustworthy() is false.
  bool resolveCountupStart(ClockConfig& cfg);

  // The only two ways this class writes a ClockConfig to disk. Both resolve the
  // count-up origin first, so a save path added later cannot skip it; that is
  // the whole reason they exist rather than calling ConfigManager directly.
  bool persistClockConfig(ClockConfig& cfg);
  bool persistClockConfig(ClockConfig& cfg, const WifiConfig& wifi);

  void populateConfigJson(JsonDocument& doc);
  // Mirrors a /api/config response body to the serial monitor. Call it after
  // the response has been sent: writing ~1KB at 74880 baud blocks for roughly
  // 150ms, which must not sit on the browser's wait.
  void logConfigJson(const JsonDocument& doc) const;

  ESP8266WebServer& server_;       // Source of request payloads and query args.
  HttpResponder& responder_;       // Sends JSON/HTML API responses.
  ClockController& clockController_;  // Executes application-level clock actions.
  ConfigManager& configManager_;      // Loads, validates, and saves configuration.
  BeepPlayer& sound_;  // Generated-beep previews for /sound.
  RtcService& rtc_;  // Stamps the count-up origin when a save resolves "now".
  RebootScheduler& rebootScheduler_;  // Schedules a reboot after WiFi changes.
};
