#pragma once

#include <ArduinoJson.h>
#include <ESP8266WebServer.h>

#include "http_responder.h"

class ClockController;
class ConfigApi;
class ConfigManager;
class NetworkTimeSync;
class RtcService;

// Handles the clock's time and timezone: reports both (with the next DST
// change and NTP state), and applies a browser-supplied time and timezone in
// one request so the two can never be half-saved.
class TimeApi {
 public:
  TimeApi(ESP8266WebServer& server, HttpResponder& responder,
          ClockController& clockController, RtcService& rtc,
          ConfigManager& configManager, ConfigApi& configApi,
          const NetworkTimeSync& networkTime)
      : m_server(server),
        m_responder(responder),
        m_clockController(clockController),
        m_rtc(rtc),
        m_configManager(configManager),
        m_configApi(configApi),
        m_networkTime(networkTime) {}

  void handleGetTime();
  void handleTimeSync();

 private:
  ESP8266WebServer& m_server;          // Source of time-sync request bodies.
  HttpResponder& m_responder;          // Sends time API responses.
  ClockController& m_clockController;  // Applies synchronized time to the application.
  RtcService& m_rtc;                   // Current UTC, the active rule, and RTC health.
  ConfigManager& m_configManager;      // Reads the stored timezone name for responses.
  ConfigApi& m_configApi;              // Persists a new timezone through the one save path.
  const NetworkTimeSync& m_networkTime;  // NTP state for the status response.
};
