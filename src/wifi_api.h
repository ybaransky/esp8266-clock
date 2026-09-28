#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <ESP8266WebServer.h>

#include "http_responder.h"
#include "reboot_scheduler.h"

class ConfigManager;
class WifiConnectionManager;

// Handles WiFi status, scan, and credential-change HTTP endpoints.
class WifiApi {
 public:
  WifiApi(ESP8266WebServer& server, HttpResponder& responder,
          ConfigManager& configManager,
          WifiConnectionManager& wifiConnectionManager,
          RebootScheduler& rebootScheduler)
      : m_server(server),
        m_responder(responder),
        m_configManager(configManager),
        m_wifiConnectionManager(wifiConnectionManager),
        m_rebootScheduler(rebootScheduler) {}

  void handleStatus();
  void handleScan();
  void handleConnect();

 private:
  ESP8266WebServer& m_server;       // Source of WiFi API request bodies.
  HttpResponder& m_responder;       // Sends WiFi API responses.
  ConfigManager& m_configManager;   // Persists requested station credentials.
  WifiConnectionManager& m_wifiConnectionManager;  // Performs scans and connection changes.
  RebootScheduler& m_rebootScheduler;  // Schedules reboot after credentials are saved.
};
