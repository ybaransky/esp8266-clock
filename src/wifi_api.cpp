#include "wifi_api.h"

#include "log.h"
#include "web_server.h"
#include "wifi_connection_manager.h"

namespace {

constexpr uint32_t kRebootDelayMs = 1500;

}  // namespace

// -----------------------------------------------------------------------------
// WifiApi
// -----------------------------------------------------------------------------

void WifiApi::handleStatus() {
  const WifiRuntimeStatus status = wifiConnectionManager_.status();
  JsonDocument doc;
  doc["mode"] = status.mode == WifiMode::kStation ? "station" : "access_point";
  doc["connected"] = status.connected;
  doc["ssid"] = status.ssid;
  doc["ip"] = status.ip;
  doc["apSsid"] = status.apSsid;
  doc["apIp"] = status.apIp;
  responder_.sendJsonDocument(200, doc);
}

void WifiApi::handleScan() {
  JsonDocument doc;
  wifiConnectionManager_.scanNetworks(doc);
  responder_.sendJsonDocument(200, doc);
}

void WifiApi::handleConnect() {
  JsonDocument doc;
  if (!responder_.parseJsonBody(doc, "/api/wifi/connect")) return;

  const String ssid = doc["ssid"] | "";
  const String password = doc["password"] | "";
  if (!wifiConnectionManager_.connectAndSave(configManager_, ssid, password)) {
    LOG_PRINTLN("/api/wifi/connect failed: SSID required");
    responder_.sendJsonError(400, "SSID required");
    return;
  }

  responder_.sendJson(200, "{\"message\":\"Saved - rebooting...\",\"reboot\":true}");
  webPortal_.scheduleReboot(kRebootDelayMs);
}

