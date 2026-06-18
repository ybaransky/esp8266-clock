#include "web_server.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

#include "config_api.h"
#include "file_api.h"
#include "http_responder.h"
#include "log.h"
#include "page_api.h"
#include "reboot_scheduler.h"
#include "wifi_api.h"
#include "wifi_connection_manager.h"

namespace {

class WebPortal : public RebootScheduler {
 public:
  WebPortal()
      : server_(80),
        responder_(server_),
        pageApi_(server_, responder_),
        configApi_(server_, responder_, *this),
        fileApi_(server_, responder_),
        wifiApi_(server_, responder_, *this) {}

  void begin() {
    if (wifiConnectionManager.status().mode == WifiMode::kAccessPoint) {
      dnsRunning_ = dnsServer_.start(53, "*", WiFi.softAPIP());
      if (!dnsRunning_) {
        LOG_PRINTLN("Failed to start captive DNS server (no socket available)");
      }
    }

    server_.on("/", HTTP_GET, []() { portal.handleRoot(); });
    server_.on("/settings", HTTP_GET, []() { portal.handleSettings(); });
    server_.on("/config", HTTP_GET, []() { portal.handleConfigDirectory(); });
    server_.on("/format", HTTP_GET, []() { portal.handleFormat(); });
    server_.on("/time-sync", HTTP_GET, []() { portal.handleTimeSyncPage(); });
    server_.on("/messages", HTTP_GET, []() { portal.handleMessagePage(); });
    server_.on("/location", HTTP_GET, []() { portal.handleLocation(); });
    server_.on("/wifi", HTTP_GET, []() { portal.handleWifiPage(); });
    server_.on("/view", HTTP_GET, []() { portal.handleViewFile(); });

    server_.on("/api/demo/test", HTTP_POST, []() { portal.handleApiDemoTest(); });
    server_.on("/api/message/test", HTTP_POST, []() { portal.handleApiMessageTest(); });
    server_.on("/api/mode", HTTP_POST, []() { portal.handleApiSetMode(); });
    server_.on("/api/brightness", HTTP_POST, []() { portal.handleApiBrightness(); });
    server_.on("/api/time", HTTP_GET, []() { portal.handleApiTime(); });
    server_.on("/api/time/sync", HTTP_POST, []() { portal.handleApiTimeSync(); });
    server_.on("/api/formats", HTTP_GET, []() { portal.handleApiFormats(); });
    server_.on("/api/config", HTTP_GET, []() { portal.handleApiGetConfig(); });
    server_.on("/api/config", HTTP_POST, []() { portal.handleApiSaveConfig(); });
    server_.on("/api/zipcode/lookup", HTTP_GET, []() { portal.handleApiZipcodeLookup(); });
    server_.on("/api/field-mismatch", HTTP_POST, []() { portal.handleApiFieldMismatch(); });

    server_.on("/api/files", HTTP_GET, []() { portal.handleApiListFiles(); });
    server_.on("/api/file", HTTP_GET, []() { portal.handleApiReadFile(); });
    server_.on("/api/file", HTTP_DELETE, []() { portal.handleApiDeleteFile(); });
    server_.on("/api/file/upload", HTTP_POST,
               []() { portal.handleApiFileUpload(); },
               []() { portal.handleApiFileUploadData(); });

    server_.on("/api/wifi/status", HTTP_GET, []() { portal.handleApiWifiStatus(); });
    server_.on("/api/wifi/scan", HTTP_GET, []() { portal.handleApiWifiScan(); });
    server_.on("/api/wifi/connect", HTTP_POST, []() { portal.handleApiWifiConnect(); });

    server_.onNotFound([]() { portal.handleCaptiveRedirect(); });
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
    const WifiRuntimeStatus status = wifiConnectionManager.status();
    if (status.mode == WifiMode::kStation && status.connected) {
      ssid = status.ssid;
      ip = status.ip;
      return;
    }
    ssid = status.apSsid;
    ip = status.apIp;
  }

  void scheduleReboot(uint32_t delayMs) override {
    pendingRebootMs_ = millis() + delayMs;
  }

  void handleRoot() { pageApi_.handleRoot(wifiConnectionManager.status()); }
  void handleSettings() { pageApi_.handleSettings(); }
  void handleConfigDirectory() { pageApi_.handleConfigDirectory(); }
  void handleFormat() { pageApi_.handleFormat(); }
  void handleTimeSyncPage() { pageApi_.handleTimeSync(); }
  void handleMessagePage() { pageApi_.handleMessage(); }
  void handleLocation() { pageApi_.handleLocation(); }
  void handleWifiPage() { pageApi_.handleWifi(); }
  void handleViewFile() { pageApi_.handleViewFile(); }

  void handleApiDemoTest() { configApi_.handleDemoTest(); }
  void handleApiMessageTest() { configApi_.handleMessageTest(); }
  void handleApiSetMode() { configApi_.handleSetMode(); }
  void handleApiBrightness() { configApi_.handleBrightness(); }
  void handleApiTime() { configApi_.handleTime(); }
  void handleApiTimeSync() { configApi_.handleTimeSync(); }
  void handleApiFormats() { configApi_.handleFormats(); }
  void handleApiGetConfig() { configApi_.handleGetConfig(); }
  void handleApiSaveConfig() { configApi_.handleSaveConfig(); }
  void handleApiZipcodeLookup() { configApi_.handleZipcodeLookup(); }
  void handleApiFieldMismatch() { configApi_.handleFieldMismatch(); }

  void handleApiListFiles() { fileApi_.handleListFiles(); }
  void handleApiReadFile() { fileApi_.handleReadFile(); }
  void handleApiDeleteFile() { fileApi_.handleDeleteFile(); }
  void handleApiFileUpload() { fileApi_.handleUpload(); }
  void handleApiFileUploadData() { fileApi_.handleUploadData(); }

  void handleApiWifiStatus() { wifiApi_.handleStatus(); }
  void handleApiWifiScan() { wifiApi_.handleScan(); }
  void handleApiWifiConnect() { wifiApi_.handleConnect(); }

  void handleCaptiveRedirect() {
    if (wifiConnectionManager.status().mode != WifiMode::kAccessPoint) {
      responder_.sendText(404, "Not found");
      return;
    }

    responder_.logRequest(302, 0);
    server_.sendHeader("Location", "http://192.168.4.1/", true);
    server_.send(302, "text/plain", "");
  }

  static WebPortal portal;

 private:
  ESP8266WebServer server_;
  HttpResponder responder_;
  PageApi pageApi_;
  ConfigApi configApi_;
  FileApi fileApi_;
  WifiApi wifiApi_;
  DNSServer dnsServer_;
  bool dnsRunning_ = false;
  uint32_t pendingRebootMs_ = 0;

};

WebPortal WebPortal::portal;

}  // namespace

void webBegin() {
  WebPortal::portal.begin();
}

void webHandleClients() {
  WebPortal::portal.handleClients();
}

void networkGetInfo(String& ssid, String& ip) {
  WebPortal::portal.getNetworkInfo(ssid, ip);
}
