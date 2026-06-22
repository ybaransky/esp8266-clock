#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>

#include "http_responder.h"
#include "reboot_scheduler.h"

struct ClockConfig;
struct WifiConfig;

class ConfigApi {
 public:
  ConfigApi(ESP8266WebServer& server,
            HttpResponder& responder,
            RebootScheduler& rebootScheduler)
      : server_(server), responder_(responder), rebootScheduler_(rebootScheduler) {}

  void handleDemoTest();
  void handleMessageTest();
  void handleSetMode();
  void handleBrightness();
  void handleTime();
  void handleTimeSync();
  void handleFormats();
  void handleGetConfig();
  void handleSaveConfig();
  void handleSunset();
  void handleZipcodeLookup();
  void handleFieldMismatch();

 private:
  void populateConfigJson(JsonDocument& doc);
  void logConfigResponse(const ClockConfig& clockConfig,
                         const WifiConfig& wifiConfig) const;

  ESP8266WebServer& server_;       // Source of request payloads and query args.
  HttpResponder& responder_;       // Sends JSON/HTML API responses.
  RebootScheduler& rebootScheduler_;  // Schedules deferred reboot after config changes.
};
