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

WebPortal::WebPortal(ClockController& clockController,
                     ConfigManager& configManager,
                     WifiConnectionManager& wifiConnectionManager,
                     RtcService& rtc)
    : server_(80),
      responder_(server_),
      configApi_(server_, responder_, clockController, configManager, *this),
      timeApi_(server_, responder_, clockController, rtc),
      fileApi_(server_, responder_),
      locationApi_(server_, responder_),
      wifiApi_(server_, responder_, configManager, wifiConnectionManager, *this),
      clockController_(clockController),
      wifiConnectionManager_(wifiConnectionManager) {}

void WebPortal::begin() {
    if (wifiConnectionManager_.status().mode == WifiMode::kAccessPoint) {
      dnsRunning_ = dnsServer_.start(53, "*", WiFi.softAPIP());
      if (!dnsRunning_) {
        LOG_PRINTLN("Failed to start captive DNS server (no socket available)");
      }
    }

    // All pages and shared assets, gzipped into flash by tools/build_web.py
    // from the sources in web/. Dynamic data flows through the JSON APIs.
    for (size_t i = 0; i < kWebAssetCount; ++i) {
      server_.on(kWebAssets[i].path, HTTP_GET, [this, i]() {
        const WebAsset& asset = kWebAssets[i];
        responder_.sendGzipProgmem(200, asset.contentType, asset.data,
                                   asset.size, asset.immutable);
      });
    }
    server_.on("/favicon.ico", HTTP_GET,
               [this]() { sendProbe204("image/x-icon"); });

    // OS connectivity probes must receive their expected response. Redirecting
    // these to Home makes Android, Apple, and Windows repeatedly show a
    // "Sign in to network" prompt for the clock's local-only AP.
    server_.on("/generate_204", HTTP_GET,
               [this]() { sendProbe204("text/plain"); });
    server_.on("/gen_204", HTTP_GET,
               [this]() { sendProbe204("text/plain"); });
    server_.on("/hotspot-detect.html", HTTP_GET, [this]() {
      sendProbeText(
          "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    });
    server_.on("/library/test/success.html", HTTP_GET, [this]() {
      sendProbeText("Success");
    });
    server_.on("/ncsi.txt", HTTP_GET,
               [this]() { sendProbeText("Microsoft NCSI"); });
    server_.on("/connecttest.txt", HTTP_GET, [this]() {
      sendProbeText("Microsoft Connect Test");
    });

    server_.on("/api/client-log", HTTP_POST,
               [this]() { handleClientLog(); });
    server_.on("/api/status", HTTP_GET, [this]() { handleApiStatus(); });

    server_.on("/api/demo/test", HTTP_POST, [this]() { configApi_.handleDemoTest(); });
    server_.on("/api/message/test", HTTP_POST, [this]() { configApi_.handleMessageTest(); });
    server_.on("/api/mode", HTTP_POST, [this]() { configApi_.handleSetMode(); });
    server_.on("/api/brightness", HTTP_POST, [this]() { configApi_.handleBrightness(); });
    server_.on("/api/time", HTTP_GET, [this]() { timeApi_.handleGetTime(); });
    server_.on("/api/time", HTTP_POST, [this]() { timeApi_.handleTimeSync(); });
    server_.on("/api/formats", HTTP_GET, [this]() { configApi_.handleFormats(); });
    server_.on("/api/config", HTTP_GET, [this]() { configApi_.handleGetConfig(); });
    server_.on("/api/config", HTTP_POST, [this]() { configApi_.handleSaveConfig(); });
    server_.on("/api/sunset", HTTP_POST,
               [this]() { locationApi_.handleSunset(); });
    server_.on("/api/zipcode/lookup", HTTP_GET,
               [this]() { locationApi_.handleZipcodeLookup(); });
    server_.on("/api/field-mismatch", HTTP_POST,
               [this]() { configApi_.handleFieldMismatch(); });

    server_.on("/api/files", HTTP_GET, [this]() { fileApi_.handleListFiles(); });
    server_.on("/api/file", HTTP_GET, [this]() { fileApi_.handleReadFile(); });
    server_.on("/api/file", HTTP_DELETE, [this]() { fileApi_.handleDeleteFile(); });
    server_.on("/api/file/upload", HTTP_POST,
               [this]() { fileApi_.handleUpload(); },
               [this]() { fileApi_.handleUploadData(); });

    server_.on("/api/wifi/status", HTTP_GET, [this]() { wifiApi_.handleStatus(); });
    server_.on("/api/wifi/scan", HTTP_GET, [this]() { wifiApi_.handleScan(); });
    server_.on("/api/wifi/connect", HTTP_POST, [this]() { wifiApi_.handleConnect(); });

    server_.onNotFound([this]() { handleCaptiveRedirect(); });
    // Leave Nagle enabled: setDefaultNoDelay(true) was tried against the
    // power-save Android client (2026-07-13) and made transfers worse --
    // more small segments means more chances to hit the phone's doze window.
    server_.begin();
    LOG_PRINTLN("HTTP server started");
}

void WebPortal::handleClients() {
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
void WebPortal::logTrafficSummary() {
    const uint32_t nowMs = millis();
    if (nowMs - lastTrafficLogMs_ < 10000) {
      return;
    }
    const uint32_t total = responder_.responseSequence();
    if ((total != lastTrafficTotal_) || (maxLoopGapMs_ > 50)) {
      LOG_PRINTF("web traffic: %lu responses (%lu probes, %lu redirects), "
                 "max loop gap %lu ms in last 10s",
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

void WebPortal::sendProbe204(const char* contentType) {
    ++probeCount_;
    responder_.send(204, contentType, "");
}

void WebPortal::sendProbeText(const char* body) {
    ++probeCount_;
    responder_.sendText(200, body);
}

  // Receives error beacons from page JavaScript (window.onerror and failed
  // /api/ fetches) so browser-side failures land in the serial timeline next
  // to the server-side request logs.
void WebPortal::handleClientLog() {
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
    LOG_PRINTF("CLIENT %s: %s",
               server_.client().remoteIP().toString().c_str(), body.c_str());
    responder_.send(204, "text/plain", "");
}

void WebPortal::getNetworkInfo(String& ssid, String& ip) const {
    const WifiRuntimeStatus status = wifiConnectionManager_.status();
    if ((status.mode == WifiMode::kStation) && status.connected) {
      ssid = status.ssid;
      ip = status.ip;
      return;
    }
    ssid = status.apSsid;
    ip = status.apIp;
}

void WebPortal::scheduleReboot(uint32_t delayMs) {
    pendingRebootMs_ = millis() + delayMs;
}

void WebPortal::handleCaptiveRedirect() {
    if (wifiConnectionManager_.status().mode != WifiMode::kAccessPoint) {
      responder_.sendText(404, "Not found");
      return;
    }

    ++redirectCount_;
    responder_.logRequest(302, 0);
    server_.sendHeader("Location", "http://192.168.4.1/", true);
    server_.send(302, "text/plain", "");
}

// Sends dynamic identity, mode, and demo state for the static home page.
void WebPortal::handleApiStatus() {
    String ssid, ip;
    getNetworkInfo(ssid, ip);
    JsonDocument doc;
    doc["name"] = ssid;
    doc["mode"] = modeName(clockController_.activeMode());
    doc["demoActive"] = clockController_.demoActive();
    responder_.sendJsonDocument(200, doc);
}
