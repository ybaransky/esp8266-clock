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
  void handleZipcodeLookup();
  void handleFieldMismatch();

 private:
  void populateConfigJson(JsonDocument& doc);
  void logConfigResponse(const ClockConfig& clockConfig,
                         const WifiConfig& wifiConfig) const;
  void sendJsonDocument(JsonDocument& doc);

  ESP8266WebServer& server_;
  HttpResponder& responder_;
  RebootScheduler& rebootScheduler_;
};
