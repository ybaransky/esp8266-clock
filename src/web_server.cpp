#include "web_server.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

#include "config_api.h"
#include "file_api.h"
#include "html.h"
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

    server_.on("/", HTTP_GET, []() {
      portal.pageApi_.handleRoot(wifiConnectionManager.status());
    });
    server_.on("/settings", HTTP_GET, []() { portal.pageApi_.sendHtml(SETTINGS_HTML); });
    server_.on("/config", HTTP_GET, []() { portal.pageApi_.sendHtml(CONFIG_JSON_HTML); });
    server_.on("/format", HTTP_GET, []() { portal.pageApi_.sendHtml(CONFIG_HTML); });
    server_.on("/time", HTTP_GET, []() { portal.pageApi_.sendHtml(TIME_SYNC_HTML); });
    server_.on("/sunset", HTTP_GET, []() { portal.pageApi_.sendHtml(SUNSET_HTML); });
    server_.on("/messages", HTTP_GET, []() { portal.pageApi_.sendHtml(MESSAGE_HTML); });
    server_.on("/location", HTTP_GET, []() { portal.pageApi_.sendHtml(LOCATION_HTML); });
    server_.on("/wifi", HTTP_GET, []() { portal.pageApi_.sendHtml(WIFI_HTML); });
    server_.on("/view", HTTP_GET, []() { portal.pageApi_.sendHtml(VIEW_FILE_HTML); });

    server_.on("/api/demo/test", HTTP_POST, []() { portal.configApi_.handleDemoTest(); });
    server_.on("/api/message/test", HTTP_POST, []() { portal.configApi_.handleMessageTest(); });
    server_.on("/api/mode", HTTP_POST, []() { portal.configApi_.handleSetMode(); });
    server_.on("/api/brightness", HTTP_POST, []() { portal.configApi_.handleBrightness(); });
    server_.on("/api/time", HTTP_GET, []() { portal.configApi_.handleTime(); });
    server_.on("/api/time", HTTP_POST, []() { portal.configApi_.handleTimeSync(); });
    server_.on("/api/formats", HTTP_GET, []() { portal.configApi_.handleFormats(); });
    server_.on("/api/config", HTTP_GET, []() { portal.configApi_.handleGetConfig(); });
    server_.on("/api/config", HTTP_POST, []() { portal.configApi_.handleSaveConfig(); });
    server_.on("/api/sunset", HTTP_POST, []() { portal.configApi_.handleSunset(); });
    server_.on("/api/zipcode/lookup", HTTP_GET,
               []() { portal.configApi_.handleZipcodeLookup(); });
    server_.on("/api/field-mismatch", HTTP_POST,
               []() { portal.configApi_.handleFieldMismatch(); });

    server_.on("/api/files", HTTP_GET, []() { portal.fileApi_.handleListFiles(); });
    server_.on("/api/file", HTTP_GET, []() { portal.fileApi_.handleReadFile(); });
    server_.on("/api/file", HTTP_DELETE, []() { portal.fileApi_.handleDeleteFile(); });
    server_.on("/api/file/upload", HTTP_POST,
               []() { portal.fileApi_.handleUpload(); },
               []() { portal.fileApi_.handleUploadData(); });

    server_.on("/api/wifi/status", HTTP_GET, []() { portal.wifiApi_.handleStatus(); });
    server_.on("/api/wifi/scan", HTTP_GET, []() { portal.wifiApi_.handleScan(); });
    server_.on("/api/wifi/connect", HTTP_POST, []() { portal.wifiApi_.handleConnect(); });

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
