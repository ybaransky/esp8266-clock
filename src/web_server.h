#pragma once

#include <Arduino.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>

#include "config_api.h"
#include "file_api.h"
#include "http_responder.h"
#include "location_api.h"
#include "reboot_scheduler.h"
#include "time_api.h"
#include "wifi_api.h"

class ClockController;
class ConfigManager;
class NetworkTimeSync;
class RtcService;
class BeepPlayer;
class WifiConnectionManager;

// Owns the HTTP/DNS servers, registers routes, and dispatches requests.
//
// Implements RebootScheduler so the API handlers it owns depend on that
// one-method interface rather than back on this class.
class WebPortal : public RebootScheduler {
 public:
  WebPortal(ClockController& clockController,
            ConfigManager& configManager,
            WifiConnectionManager& wifiConnectionManager,
            RtcService& rtc,
            BeepPlayer& beepPlayer,
            const NetworkTimeSync& networkTime);

  void begin();
  void handleClients();
  void getNetworkInfo(String& ssid, String& ip) const;
  void scheduleReboot(uint32_t delayMs) override;

 private:
  void logTrafficSummary();
  void sendProbe204(const char* contentType);
  void sendProbeText(const char* body);
  void handleClientLog();
  void handleCaptiveRedirect();
  void handleApiStatus();

  ESP8266WebServer m_server;  // HTTP server on port 80.
  HttpResponder m_responder;  // Shared response helper.
  ConfigApi m_configApi;  // Display/configuration endpoints.
  TimeApi m_timeApi;  // RTC read and synchronization endpoints.
  FileApi m_fileApi;  // LittleFS file-management endpoints.
  LocationApi m_locationApi;  // Location and ZIP-code endpoints.
  WifiApi m_wifiApi;  // WiFi status/scan/connect endpoints.
  ClockController& m_clockController;  // Live application actions and status.
  WifiConnectionManager& m_wifiConnectionManager;  // Network operations.
  DNSServer m_dnsServer;  // Captive-portal DNS responder.
  bool m_dnsRunning = false;  // True after captive DNS starts.
  uint32_t m_pendingRebootMs = 0;  // Deferred reboot deadline.
  uint32_t m_probeCount = 0;  // Connectivity-probe responses served.
  uint32_t m_redirectCount = 0;  // Captive-portal redirects served.
  uint32_t m_lastTrafficLogMs = 0;  // Last traffic-summary time.
  uint32_t m_lastTrafficTotal = 0;  // Response count at last summary.
  uint32_t m_lastProbeCount = 0;  // Probe count at last summary.
  uint32_t m_lastRedirectCount = 0;  // Redirect count at last summary.
  uint32_t m_lastHandleClientsMs = 0;  // Previous client-service time.
  uint32_t m_maxLoopGapMs = 0;  // Largest service gap this period.
};
