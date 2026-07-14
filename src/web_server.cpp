#include "web_server.h"

#include <Arduino.h>
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <ESP8266WiFi.h>

#include "clock_controller.h"
#include "config_api.h"
#include "config_validation.h"
#include "file_api.h"
#include "http_responder.h"
#include "log.h"
#include "location_api.h"
#include "time_api.h"
#include "wifi_api.h"
#include "wifi_connection_manager.h"
#include "generated_web_assets.h"

namespace {

// Owns the HTTP/DNS servers, registers routes, and dispatches requests to domain APIs.
class WebPortal {
 public:
  WebPortal(ClockController& clockController,
            ConfigManager& configManager,
            WifiConnectionManager& wifiConnectionManager,
            RtcService& rtc)
      : server_(80),
        responder_(server_),
        configApi_(server_, responder_, clockController, configManager),
        timeApi_(server_, responder_, clockController, rtc),
        fileApi_(server_, responder_),
        locationApi_(server_, responder_),
        wifiApi_(server_, responder_, configManager, wifiConnectionManager),
        clockController_(clockController),
        configManager_(configManager),
        wifiConnectionManager_(wifiConnectionManager) {}

  void begin() {
    if (wifiConnectionManager_.status().mode == WifiMode::kAccessPoint) {
      dnsRunning_ = dnsServer_.start(53, "*", WiFi.softAPIP());
      if (!dnsRunning_) {
        LOG_PRINTLN("Failed to start captive DNS server (no socket available)");
      }
    }

    // All pages and shared assets, gzipped into flash by tools/build_web.py
    // from the sources in web/. Dynamic data flows through the JSON APIs.
    for (size_t i = 0; i < kWebAssetCount; ++i) {
      server_.on(kWebAssets[i].path, HTTP_GET, [i]() {
        const WebAsset& asset = kWebAssets[i];
        activePortal->responder_.sendGzipProgmem(200, asset.contentType,
                                                 asset.data, asset.size,
                                                 asset.immutable);
      });
    }
    server_.on("/favicon.ico", HTTP_GET,
               []() { activePortal->sendProbe204("image/x-icon"); });

    // OS connectivity probes must receive their expected response. Redirecting
    // these to Home makes Android, Apple, and Windows repeatedly show a
    // "Sign in to network" prompt for the clock's local-only AP.
    server_.on("/generate_204", HTTP_GET,
               []() { activePortal->sendProbe204("text/plain"); });
    server_.on("/gen_204", HTTP_GET,
               []() { activePortal->sendProbe204("text/plain"); });
    server_.on("/hotspot-detect.html", HTTP_GET, []() {
      activePortal->sendProbeText(
          "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    });
    server_.on("/library/test/success.html", HTTP_GET, []() {
      activePortal->sendProbeText("Success");
    });
    server_.on("/ncsi.txt", HTTP_GET,
               []() { activePortal->sendProbeText("Microsoft NCSI"); });
    server_.on("/connecttest.txt", HTTP_GET, []() {
      activePortal->sendProbeText("Microsoft Connect Test");
    });

    server_.on("/api/client-log", HTTP_POST,
               []() { activePortal->handleClientLog(); });
    server_.on("/api/status", HTTP_GET, []() { activePortal->handleApiStatus(); });

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
    // Leave Nagle enabled: setDefaultNoDelay(true) was tried against the
    // power-save Android client (2026-07-13) and made transfers worse --
    // more small segments means more chances to hit the phone's doze window.
    server_.begin();
    LOG_PRINTLN("HTTP server started");
  }

  void handleClients() {
    // A large gap between calls means the main loop stalled (e.g. display
    // writes); queued requests experience it as time-to-first-byte.
    const uint32_t entryMs = millis();
    if (lastHandleClientsMs_ != 0) {
      const uint32_t gap = entryMs - lastHandleClientsMs_;
      if (gap > maxLoopGapMs_) {
        maxLoopGapMs_ = gap;
      }
    }
    lastHandleClientsMs_ = entryMs;

    if (dnsRunning_) {
      dnsServer_.processNextRequest();
    }
    const uint32_t responseBefore = responder_.responseSequence();
    const uint32_t startedUs = micros();
    server_.handleClient();
    if (responder_.responseSequence() != responseBefore) {
      responder_.logCompletion(micros() - startedUs);
    }
    if ((pendingRebootMs_ != 0) && (static_cast<long>(millis() - pendingRebootMs_) >= 0)) {
      LOG_PRINTLN("Rebooting...");
      ESP.restart();
    }
    logTrafficSummary();
  }

  // Every 10s, summarize how much captive-portal noise (probes/redirects) the
  // single-threaded server handled. Page loads competing with a probe storm
  // are a prime suspect for stalled or truncated transfers in AP mode.
  void logTrafficSummary() {
    const uint32_t nowMs = millis();
    if (nowMs - lastTrafficLogMs_ < 10000) {
      return;
    }
    const uint32_t total = responder_.responseSequence();
    if ((total != lastTrafficTotal_) || (maxLoopGapMs_ > 50)) {
      LOG_PRINTF("web traffic: %lu responses (%lu probes, %lu redirects), "
                 "max loop gap %lu ms in last 10s\n",
                 static_cast<unsigned long>(total - lastTrafficTotal_),
                 static_cast<unsigned long>(probeCount_ - lastProbeCount_),
                 static_cast<unsigned long>(redirectCount_ - lastRedirectCount_),
                 static_cast<unsigned long>(maxLoopGapMs_));
    }
    lastTrafficLogMs_ = nowMs;
    lastTrafficTotal_ = total;
    lastProbeCount_ = probeCount_;
    lastRedirectCount_ = redirectCount_;
    maxLoopGapMs_ = 0;
  }

  void sendProbe204(const char* contentType) {
    ++probeCount_;
    responder_.send(204, contentType, "");
  }

  void sendProbeText(const char* body) {
    ++probeCount_;
    responder_.sendText(200, body);
  }

  // Receives error beacons from page JavaScript (window.onerror and failed
  // /api/ fetches) so browser-side failures land in the serial timeline next
  // to the server-side request logs.
  void handleClientLog() {
    String body = server_.arg("plain");
    if (body.length() > 160) {
      body.remove(160);
    }
    for (size_t i = 0; i < body.length(); ++i) {
      const char c = body[i];
      if ((c < 32) || (c > 126)) {
        body.setCharAt(i, '.');
      }
    }
    LOG_PRINTF("CLIENT %s: %s\n",
               server_.client().remoteIP().toString().c_str(), body.c_str());
    responder_.send(204, "text/plain", "");
  }

  void getNetworkInfo(String& ssid, String& ip) {
    const WifiRuntimeStatus status = wifiConnectionManager_.status();
    if ((status.mode == WifiMode::kStation) && status.connected) {
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

    ++redirectCount_;
    responder_.logRequest(302, 0);
    server_.sendHeader("Location", "http://192.168.4.1/", true);
    server_.send(302, "text/plain", "");
  }

  static WebPortal* activePortal;  // Singleton bridge used by route callbacks.

 private:
  // Dynamic values for the static home page: device name and configured mode.
  void handleApiStatus() {
    String ssid, ip;
    getNetworkInfo(ssid, ip);
    JsonDocument doc;
    doc["name"] = ssid;
    doc["mode"] = modeName(clockController_.activeMode());
    responder_.sendJsonDocument(200, doc);
  }

  ESP8266WebServer server_;        // HTTP server on port 80.
  HttpResponder responder_;        // Shared response helper.
  ConfigApi configApi_;            // Display/configuration API endpoints.
  TimeApi timeApi_;                // RTC read and synchronization endpoints.
  FileApi fileApi_;                // LittleFS file-management endpoints.
  LocationApi locationApi_;        // Location and ZIP-code endpoints.
  WifiApi wifiApi_;                // WiFi status/scan/connect endpoints.
  ClockController& clockController_; // Live state; avoids a LittleFS read on `/`.
  ConfigManager& configManager_;  // Persistent configuration used by portal actions.
  WifiConnectionManager& wifiConnectionManager_;  // Network status and connection operations.
  DNSServer dnsServer_;            // Captive portal DNS responder.
  bool dnsRunning_ = false;        // True when captive DNS started successfully.
  uint32_t pendingRebootMs_ = 0;   // millis() deadline for deferred reboot.
  uint32_t probeCount_ = 0;        // OS connectivity-probe responses served.
  uint32_t redirectCount_ = 0;     // Captive-portal 302 redirects served.
  uint32_t lastTrafficLogMs_ = 0;  // Last traffic-summary log time.
  uint32_t lastTrafficTotal_ = 0;  // responseSequence() at last summary.
  uint32_t lastProbeCount_ = 0;    // probeCount_ at last summary.
  uint32_t lastRedirectCount_ = 0; // redirectCount_ at last summary.
  uint32_t lastHandleClientsMs_ = 0;  // Previous handleClients() entry time.
  uint32_t maxLoopGapMs_ = 0;      // Largest gap between calls this period.

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
