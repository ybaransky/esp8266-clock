#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>

#include "config_api.h"
#include "file_api.h"
#include "http_responder.h"
#include "location_api.h"
#include "time_api.h"
#include "wifi_api.h"

class ClockController;
class ConfigManager;
class RtcService;
class WifiConnectionManager;

// Owns the HTTP/DNS servers, registers routes, and dispatches requests.
class WebPortal {
 public:
  WebPortal(ClockController& clockController,
            ConfigManager& configManager,
            WifiConnectionManager& wifiConnectionManager,
            RtcService& rtc);

  void begin();
  void handleClients();
  void getNetworkInfo(String& ssid, String& ip) const;
  void scheduleReboot(uint32_t delayMs);

 private:
  void logTrafficSummary();
  void sendProbe204(const char* contentType);
  void sendProbeText(const char* body);
  void handleClientLog();
  void handleCaptiveRedirect();
  void handleApiStatus();

  ESP8266WebServer server_;  // HTTP server on port 80.
  HttpResponder responder_;  // Shared response helper.
  ConfigApi configApi_;  // Display/configuration endpoints.
  TimeApi timeApi_;  // RTC read and synchronization endpoints.
  FileApi fileApi_;  // LittleFS file-management endpoints.
  LocationApi locationApi_;  // Location and ZIP-code endpoints.
  WifiApi wifiApi_;  // WiFi status/scan/connect endpoints.
  ClockController& clockController_;  // Live application actions and status.
  WifiConnectionManager& wifiConnectionManager_;  // Network operations.
  DNSServer dnsServer_;  // Captive-portal DNS responder.
  bool dnsRunning_ = false;  // True after captive DNS starts.
  uint32_t pendingRebootMs_ = 0;  // Deferred reboot deadline.
  uint32_t probeCount_ = 0;  // Connectivity-probe responses served.
  uint32_t redirectCount_ = 0;  // Captive-portal redirects served.
  uint32_t lastTrafficLogMs_ = 0;  // Last traffic-summary time.
  uint32_t lastTrafficTotal_ = 0;  // Response count at last summary.
  uint32_t lastProbeCount_ = 0;  // Probe count at last summary.
  uint32_t lastRedirectCount_ = 0;  // Redirect count at last summary.
  uint32_t lastHandleClientsMs_ = 0;  // Previous client-service time.
  uint32_t maxLoopGapMs_ = 0;  // Largest service gap this period.
};
