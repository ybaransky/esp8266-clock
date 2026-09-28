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
                     RtcService& rtc,
                     BeepPlayer& beepPlayer)
    : m_server(80),
      m_responder(m_server),
      m_configApi(m_server, m_responder, clockController, configManager,
                 beepPlayer, rtc, *this),
      m_timeApi(m_server, m_responder, clockController, rtc),
      m_fileApi(m_server, m_responder),
      m_locationApi(m_server, m_responder),
      m_wifiApi(m_server, m_responder, configManager, wifiConnectionManager, *this),
      m_clockController(clockController),
      m_wifiConnectionManager(wifiConnectionManager) {}

void WebPortal::begin() {
    if (m_wifiConnectionManager.status().mode == WifiMode::kAccessPoint) {
      m_dnsRunning = m_dnsServer.start(53, "*", WiFi.softAPIP());
      if (!m_dnsRunning) {
        LOG_PRINTLN("Failed to start captive DNS server (no socket available)");
      }
    }

    // All pages and shared assets, gzipped into flash by tools/build_web.py
    // from the sources in web/. Dynamic data flows through the JSON APIs.
    for (size_t i = 0; i < kWebAssetCount; ++i) {
      m_server.on(kWebAssets[i].path, HTTP_GET, [this, i]() {
        const WebAsset& asset = kWebAssets[i];
        m_responder.sendGzipProgmem(200, asset.contentType, asset.data,
                                   asset.size, asset.immutable);
      });
    }
    m_server.on("/favicon.ico", HTTP_GET,
               [this]() { sendProbe204("image/x-icon"); });

    // OS connectivity probes must receive their expected response. Redirecting
    // these to Home makes Android, Apple, and Windows repeatedly show a
    // "Sign in to network" prompt for the clock's local-only AP.
    m_server.on("/generate_204", HTTP_GET,
               [this]() { sendProbe204("text/plain"); });
    m_server.on("/gen_204", HTTP_GET,
               [this]() { sendProbe204("text/plain"); });
    m_server.on("/hotspot-detect.html", HTTP_GET, [this]() {
      sendProbeText(
          "<HTML><HEAD><TITLE>Success</TITLE></HEAD><BODY>Success</BODY></HTML>");
    });
    m_server.on("/library/test/success.html", HTTP_GET, [this]() {
      sendProbeText("Success");
    });
    m_server.on("/ncsi.txt", HTTP_GET,
               [this]() { sendProbeText("Microsoft NCSI"); });
    m_server.on("/connecttest.txt", HTTP_GET, [this]() {
      sendProbeText("Microsoft Connect Test");
    });

    m_server.on("/api/client-log", HTTP_POST,
               [this]() { handleClientLog(); });
    m_server.on("/api/status", HTTP_GET, [this]() { handleApiStatus(); });

    m_server.on("/api/demo/test", HTTP_POST, [this]() { m_configApi.handleDemoTest(); });
    m_server.on("/api/message/test", HTTP_POST, [this]() { m_configApi.handleMessageTest(); });
    m_server.on("/api/mode", HTTP_POST, [this]() { m_configApi.handleSetMode(); });
    m_server.on("/api/brightness", HTTP_POST, [this]() { m_configApi.handleBrightness(); });
    m_server.on("/api/time", HTTP_GET, [this]() { m_timeApi.handleGetTime(); });
    m_server.on("/api/time", HTTP_POST, [this]() { m_timeApi.handleTimeSync(); });
    m_server.on("/api/formats", HTTP_GET, [this]() { m_configApi.handleFormats(); });
    m_server.on("/api/sound/test", HTTP_POST,
               [this]() { m_configApi.handleSoundTest(); });
    m_server.on("/api/config", HTTP_GET, [this]() { m_configApi.handleGetConfig(); });
    m_server.on("/api/config", HTTP_POST, [this]() { m_configApi.handleSaveConfig(); });
    m_server.on("/api/sunset", HTTP_POST,
               [this]() { m_locationApi.handleSunset(); });
    m_server.on("/api/zipcode/lookup", HTTP_GET,
               [this]() { m_locationApi.handleZipcodeLookup(); });
    m_server.on("/api/field-mismatch", HTTP_POST,
               [this]() { m_configApi.handleFieldMismatch(); });

    m_server.on("/api/files", HTTP_GET, [this]() { m_fileApi.handleListFiles(); });
    m_server.on("/api/file", HTTP_GET, [this]() { m_fileApi.handleReadFile(); });
    m_server.on("/api/file", HTTP_DELETE, [this]() { m_fileApi.handleDeleteFile(); });
    m_server.on("/api/file/upload", HTTP_POST,
               [this]() { m_fileApi.handleUpload(); },
               [this]() { m_fileApi.handleUploadData(); });

    m_server.on("/api/wifi/status", HTTP_GET, [this]() { m_wifiApi.handleStatus(); });
    m_server.on("/api/wifi/scan", HTTP_GET, [this]() { m_wifiApi.handleScan(); });
    m_server.on("/api/wifi/connect", HTTP_POST, [this]() { m_wifiApi.handleConnect(); });

    m_server.onNotFound([this]() { handleCaptiveRedirect(); });
    // Leave Nagle enabled: setDefaultNoDelay(true) was tried against the
    // power-save Android client (2026-07-13) and made transfers worse --
    // more small segments means more chances to hit the phone's doze window.
    m_server.begin();
    LOG_PRINTLN("HTTP server started");
}

void WebPortal::handleClients() {
    // A large gap between calls means the main loop stalled (e.g. display
    // writes); queued requests experience it as time-to-first-byte.
    const uint32_t entryMs = millis();
    if (m_lastHandleClientsMs != 0) {
      const uint32_t gap = entryMs - m_lastHandleClientsMs;
      if (gap > m_maxLoopGapMs) {
        m_maxLoopGapMs = gap;
      }
    }
    m_lastHandleClientsMs = entryMs;

    if (m_dnsRunning) {
      m_dnsServer.processNextRequest();
    }
    const uint32_t responseBefore = m_responder.responseSequence();
    const uint32_t startedUs = micros();
    m_server.handleClient();
    if (m_responder.responseSequence() != responseBefore) {
      m_responder.logCompletion(micros() - startedUs);
    }
    if ((m_pendingRebootMs != 0) && (static_cast<long>(millis() - m_pendingRebootMs) >= 0)) {
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
    if (nowMs - m_lastTrafficLogMs < 10000) {
      return;
    }
    const uint32_t total = m_responder.responseSequence();
    if ((total != m_lastTrafficTotal) || (m_maxLoopGapMs > 50)) {
      LOG_PRINTF("web traffic: %lu responses (%lu probes, %lu redirects), "
                 "max loop gap %lu ms in last 10s",
                 static_cast<unsigned long>(total - m_lastTrafficTotal),
                 static_cast<unsigned long>(m_probeCount - m_lastProbeCount),
                 static_cast<unsigned long>(m_redirectCount - m_lastRedirectCount),
                 static_cast<unsigned long>(m_maxLoopGapMs));
    }
    m_lastTrafficLogMs = nowMs;
    m_lastTrafficTotal = total;
    m_lastProbeCount = m_probeCount;
    m_lastRedirectCount = m_redirectCount;
    m_maxLoopGapMs = 0;
}

void WebPortal::sendProbe204(const char* contentType) {
    ++m_probeCount;
    m_responder.send(204, contentType, "");
}

void WebPortal::sendProbeText(const char* body) {
    ++m_probeCount;
    m_responder.sendText(200, body);
}

  // Receives error beacons from page JavaScript (window.onerror and failed
  // /api/ fetches) so browser-side failures land in the serial timeline next
  // to the server-side request logs.
void WebPortal::handleClientLog() {
    String body = m_server.arg("plain");
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
               m_server.client().remoteIP().toString().c_str(), body.c_str());
    m_responder.send(204, "text/plain", "");
}

void WebPortal::getNetworkInfo(String& ssid, String& ip) const {
    const WifiRuntimeStatus status = m_wifiConnectionManager.status();
    if ((status.mode == WifiMode::kStation) && status.connected) {
      ssid = status.ssid;
      ip = status.ip;
      return;
    }
    ssid = status.apSsid;
    ip = status.apIp;
}

void WebPortal::scheduleReboot(uint32_t delayMs) {
    m_pendingRebootMs = millis() + delayMs;
}

void WebPortal::handleCaptiveRedirect() {
    if (m_wifiConnectionManager.status().mode != WifiMode::kAccessPoint) {
      m_responder.sendText(404, "Not found");
      return;
    }

    ++m_redirectCount;
    m_responder.logRequest(302, 0);
    m_server.sendHeader("Location", "http://192.168.4.1/", true);
    m_server.send(302, "text/plain", "");
}

// Sends dynamic identity, mode, and demo state for the static home page.
void WebPortal::handleApiStatus() {
    String ssid, ip;
    getNetworkInfo(ssid, ip);
    JsonDocument doc;
    doc["name"] = ssid;
    doc["mode"] = modeName(m_clockController.activeMode());
    doc["demoActive"] = m_clockController.demoActive();
    m_responder.sendJsonDocument(200, doc);
}
