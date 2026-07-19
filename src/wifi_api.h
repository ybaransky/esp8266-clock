#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>

#include "http_responder.h"

class ConfigManager;
class WebPortal;
class WifiConnectionManager;

// Handles WiFi status, scan, and credential-change HTTP endpoints.
class WifiApi {
 public:
  WifiApi(ESP8266WebServer& server, HttpResponder& responder,
          ConfigManager& configManager,
          WifiConnectionManager& wifiConnectionManager,
          WebPortal& webPortal)
      : server_(server),
        responder_(responder),
        configManager_(configManager),
        wifiConnectionManager_(wifiConnectionManager),
        webPortal_(webPortal) {}

  void handleStatus();
  void handleScan();
  void handleConnect();

 private:
  ESP8266WebServer& server_;       // Source of WiFi API request bodies.
  HttpResponder& responder_;       // Sends WiFi API responses.
  ConfigManager& configManager_;   // Persists requested station credentials.
  WifiConnectionManager& wifiConnectionManager_;  // Performs scans and connection changes.
  WebPortal& webPortal_;  // Schedules reboot after credentials are saved.
};
