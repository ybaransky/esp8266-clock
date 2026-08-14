#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>

#include "http_responder.h"

struct ClockConfig;
struct WifiConfig;
class ClockController;
class ConfigManager;
class WebPortal;

// Handles clock-configuration HTTP endpoints by validating input and invoking application actions.
class ConfigApi {
 public:
  ConfigApi(ESP8266WebServer& server, HttpResponder& responder,
            ClockController& clockController,
            ConfigManager& configManager,
            WebPortal& webPortal)
      : server_(server),
        responder_(responder),
        clockController_(clockController),
        configManager_(configManager),
        webPortal_(webPortal) {}

  void handleDemoTest();
  void handleMessageTest();
  void handleSetMode();
  void handleBrightness();
  void handleFormats();
  void handleSounds();
  void handleSoundTest();
  void handleGetConfig();
  void handleSaveConfig();
  void handleFieldMismatch();

 private:
  void populateConfigJson(JsonDocument& doc);
  // Mirrors a /api/config response body to the serial monitor. Call it after
  // the response has been sent: writing ~1KB at 74880 baud blocks for roughly
  // 150ms, which must not sit on the browser's wait.
  void logConfigJson(const JsonDocument& doc) const;

  ESP8266WebServer& server_;       // Source of request payloads and query args.
  HttpResponder& responder_;       // Sends JSON/HTML API responses.
  ClockController& clockController_;  // Executes application-level clock actions.
  ConfigManager& configManager_;      // Loads, validates, and saves configuration.
  WebPortal& webPortal_;  // Schedules a reboot after WiFi changes.
};
