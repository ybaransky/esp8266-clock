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
  void handleGetConfig();
  void handleSaveConfig();
  void handleFieldMismatch();

 private:
  // Deserializes the request body into doc. On failure, logs, sends 400, and returns false.
  bool parseJsonBody(JsonDocument& doc, const char* route);

  void populateConfigJson(JsonDocument& doc);

  ESP8266WebServer& server_;       // Source of request payloads and query args.
  HttpResponder& responder_;       // Sends JSON/HTML API responses.
  ClockController& clockController_;  // Executes application-level clock actions.
  ConfigManager& configManager_;      // Loads, validates, and saves configuration.
  WebPortal& webPortal_;  // Schedules a reboot after WiFi changes.
};
