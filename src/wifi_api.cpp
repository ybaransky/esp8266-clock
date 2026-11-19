#include "wifi_api.h"

#include "log.h"

#include "wifi_connection_manager.h"

namespace {

constexpr uint32_t kRebootDelayMs = 1500;  // Lets the HTTP response flush before restarting.

}  // namespace

// -----------------------------------------------------------------------------
// WifiApi
// -----------------------------------------------------------------------------

void WifiApi::handleStatus() {
  const WifiRuntimeStatus status = m_wifiConnectionManager.status();
  JsonDocument doc;
  doc["mode"] = status.mode == WifiMode::kStation ? "station" : "access_point";
  doc["connected"] = status.connected;
  doc["ssid"] = status.ssid;
  doc["ip"] = status.ip;
  doc["apSsid"] = status.apSsid;
  doc["apIp"] = status.apIp;
  m_responder.sendJsonDocument(200, doc);
}

void WifiApi::handleScan() {
  JsonDocument doc;
  m_wifiConnectionManager.scanNetworks(doc);
  m_responder.sendJsonDocument(200, doc);
}

void WifiApi::handleConnect() {
  JsonDocument doc;
  if (!m_responder.parseJsonBody(doc, "/api/wifi/connect")) return;

  const String ssid = doc["ssid"] | "";
  const String password = doc["password"] | "";
  if (ssid.isEmpty()) {
    LOG_PRINTLN("/api/wifi/connect failed: SSID required");
    m_responder.sendJsonError(400, F("SSID required"));
    return;
  }
  if (!m_wifiConnectionManager.connectAndSave(m_configManager, ssid, password)) {
    LOG_PRINTLN("/api/wifi/connect failed: configuration write failed");
    m_responder.sendJsonError(500, F("Configuration write failed"));
    return;
  }

  m_responder.sendJson(200, F("{\"message\":\"Saved - rebooting...\",\"reboot\":true}"));
  m_rebootScheduler.scheduleReboot(kRebootDelayMs);
}

