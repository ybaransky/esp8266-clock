#include "web_server.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

#include "clock_controller.h"
#include "config_api.h"
#include "file_api.h"
#include "html.h"
#include "http_responder.h"
#include "log.h"
#include "location_api.h"
#include "page_api.h"
#include "time_api.h"
#include "wifi_api.h"
#include "wifi_connection_manager.h"

namespace {

class WebPortal {
 public:
  WebPortal(ClockController& clockController,
            ConfigManager& configManager,
            WifiConnectionManager& wifiConnectionManager,
            RtcService& rtc)
      : server_(80),
        responder_(server_),
        pageApi_(server_, responder_, configManager),
        configApi_(server_, responder_, clockController, configManager),
        timeApi_(server_, responder_, clockController, rtc),
        fileApi_(server_, responder_),
        locationApi_(server_, responder_),
        wifiApi_(server_, responder_, configManager, wifiConnectionManager),
        wifiConnectionManager_(wifiConnectionManager) {}

  void begin() {
    if (wifiConnectionManager_.status().mode == WifiMode::kAccessPoint) {
      dnsRunning_ = dnsServer_.start(53, "*", WiFi.softAPIP());
      if (!dnsRunning_) {
        LOG_PRINTLN("Failed to start captive DNS server (no socket available)");
      }
    }

    server_.on("/", HTTP_GET, []() {
      activePortal->pageApi_.handleRoot(
          activePortal->wifiConnectionManager_.status());
    });
    server_.on("/settings", HTTP_GET, []() { activePortal->pageApi_.sendHtml(SETTINGS_HTML); });
    server_.on("/config", HTTP_GET, []() { activePortal->pageApi_.sendHtml(CONFIG_JSON_HTML); });
    server_.on("/format", HTTP_GET, []() { activePortal->pageApi_.sendHtml(CONFIG_HTML); });
    server_.on("/time", HTTP_GET, []() { activePortal->pageApi_.sendHtml(TIME_SYNC_HTML); });
    server_.on("/sunset", HTTP_GET, []() { activePortal->pageApi_.sendHtml(SUNSET_HTML); });
    server_.on("/messages", HTTP_GET, []() { activePortal->pageApi_.sendHtml(MESSAGE_HTML); });
    server_.on("/location", HTTP_GET, []() { activePortal->pageApi_.sendHtml(LOCATION_HTML); });
    server_.on("/wifi", HTTP_GET, []() { activePortal->pageApi_.sendHtml(WIFI_HTML); });
    server_.on("/view", HTTP_GET, []() { activePortal->pageApi_.sendHtml(VIEW_FILE_HTML); });

    server_.on("/api/demo/test", HTTP_POST, []() { activePortal->configApi_.handleDemoTest(); });
    server_.on("/api/message/test", HTTP_POST, []() { activePortal->configApi_.handleMessageTest(); });
    server_.on("/api/mode", HTTP_POST, []() { activePortal->configApi_.handleSetMode(); });
    server_.on("/api/brightness", HTTP_POST, []() { activePortal->configApi_.handleBrightness(); });
    server_.on("/api/time", HTTP_GET, []() { activePortal->timeApi_.handleGetTime(); });
    server_.on("/api/time", HTTP_POST, []() { activePortal->timeApi_.handleTimeSync(); });
    server_.on("/api/formats", HTTP_GET, []() { activePortal->configApi_.handleFormats(); });
    server_.on("/api/config", HTTP_GET, []() { activePortal->configApi_.handleGetConfig(); });
    server_.on("/api/config", HTTP_POST, []() { activePortal->configApi_.handleSaveConfig(); });
    server_.on("/api/sunset", HTTP_POST,
               []() { activePortal->locationApi_.handleSunset(); });
    server_.on("/api/zipcode/lookup", HTTP_GET,
               []() { activePortal->locationApi_.handleZipcodeLookup(); });
    server_.on("/api/field-mismatch", HTTP_POST,
               []() { activePortal->configApi_.handleFieldMismatch(); });

    server_.on("/api/files", HTTP_GET, []() { activePortal->fileApi_.handleListFiles(); });
    server_.on("/api/file", HTTP_GET, []() { activePortal->fileApi_.handleReadFile(); });
    server_.on("/api/file", HTTP_DELETE, []() { activePortal->fileApi_.handleDeleteFile(); });
    server_.on("/api/file/upload", HTTP_POST,
               []() { activePortal->fileApi_.handleUpload(); },
               []() { activePortal->fileApi_.handleUploadData(); });

    server_.on("/api/wifi/status", HTTP_GET, []() { activePortal->wifiApi_.handleStatus(); });
    server_.on("/api/wifi/scan", HTTP_GET, []() { activePortal->wifiApi_.handleScan(); });
    server_.on("/api/wifi/connect", HTTP_POST, []() { activePortal->wifiApi_.handleConnect(); });

    server_.onNotFound([]() { activePortal->handleCaptiveRedirect(); });
    server_.begin();
    LOG_PRINTLN("HTTP server started");
  }

  void handleClients() {
    if (dnsRunning_) {
      dnsServer_.processNextRequest();
    }
    server_.handleClient();
    if (pendingRebootMs_ != 0 && static_cast<long>(millis() - pendingRebootMs_) >= 0) {
      LOG_PRINTLN("Rebooting...");
      ESP.restart();
    }
  }

  void getNetworkInfo(String& ssid, String& ip) {
    const WifiRuntimeStatus status = wifiConnectionManager_.status();
    if (status.mode == WifiMode::kStation && status.connected) {
      ssid = status.ssid;
      ip = status.ip;
      return;
    }
    ssid = status.apSsid;
    ip = status.apIp;
  }

  void scheduleReboot(uint32_t delayMs) {
    pendingRebootMs_ = millis() + delayMs;
  }

  void handleCaptiveRedirect() {
    if (wifiConnectionManager_.status().mode != WifiMode::kAccessPoint) {
      responder_.sendText(404, "Not found");
      return;
    }

    responder_.logRequest(302, 0);
    server_.sendHeader("Location", "http://192.168.4.1/", true);
    server_.send(302, "text/plain", "");
  }

  static WebPortal* activePortal;

 private:
  ESP8266WebServer server_;        // HTTP server on port 80.
  HttpResponder responder_;        // Shared response helper.
  PageApi pageApi_;                // HTML page endpoints.
  ConfigApi configApi_;            // Display/configuration API endpoints.
  TimeApi timeApi_;                // RTC read and synchronization endpoints.
  FileApi fileApi_;                // LittleFS file-management endpoints.
  LocationApi locationApi_;        // Location and ZIP-code endpoints.
  WifiApi wifiApi_;                // WiFi status/scan/connect endpoints.
  WifiConnectionManager& wifiConnectionManager_;
  DNSServer dnsServer_;            // Captive portal DNS responder.
  bool dnsRunning_ = false;        // True when captive DNS started successfully.
  uint32_t pendingRebootMs_ = 0;   // millis() deadline for deferred reboot.

};

WebPortal* WebPortal::activePortal = nullptr;

}  // namespace

void webBegin(ClockController& clockController,
              ConfigManager& configManager,
              WifiConnectionManager& wifiConnectionManager,
              RtcService& rtc) {
  static WebPortal portal(clockController, configManager, wifiConnectionManager, rtc);
  WebPortal::activePortal = &portal;
  portal.begin();
}

void webHandleClients() {
  WebPortal::activePortal->handleClients();
}

void networkGetInfo(String& ssid, String& ip) {
  WebPortal::activePortal->getNetworkInfo(ssid, ip);
}

void webScheduleReboot(uint32_t delayMs) {
  WebPortal::activePortal->scheduleReboot(delayMs);
}
