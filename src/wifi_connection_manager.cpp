#include "wifi_connection_manager.h"

#include <ESP8266WiFi.h>

#include "log.h"

namespace {

constexpr uint32_t kStationConnectTimeoutMs = 15000;
constexpr uint16_t kStationConnectPollMs = 250;

WiFiEventHandler apStationConnectedHandler;

bool isSecureNetwork(uint8_t encryptionType) {
  return encryptionType != ENC_TYPE_NONE;
}

void logAccessPointStationConnected(const WiFiEventSoftAPModeStationConnected& event) {
  LOG_PRINTF("AP client connected  MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
             event.mac[0], event.mac[1], event.mac[2],
             event.mac[3], event.mac[4], event.mac[5]);
}

}  // namespace

void WifiConnectionManager::begin(const WifiConfig& config) {
  config_ = config;
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);

  if (!config_.staSsid.isEmpty() &&
      tryStationConnect(config_.staSsid, config_.staPassword)) {
    mode_ = WifiMode::kStation;
    LOG_PRINTF("STA \"%s\" connected  IP: %s\n",
               config_.staSsid.c_str(),
               WiFi.localIP().toString().c_str());
    return;
  }

  startAccessPoint();
}

void WifiConnectionManager::tick() {
  // Reserved for future reconnect/backoff behavior.
}

WifiRuntimeStatus WifiConnectionManager::status() const {
  WifiRuntimeStatus runtime;
  runtime.mode = mode_;
  runtime.connected = WiFi.status() == WL_CONNECTED;
  runtime.ssid = runtime.connected ? WiFi.SSID() : String();
  runtime.ip = runtime.connected ? WiFi.localIP().toString() : String();
  runtime.apSsid = config_.apSsid;
  runtime.apIp = WiFi.softAPIP().toString();
  return runtime;
}

void WifiConnectionManager::scanNetworks(JsonDocument& doc) {
  JsonArray networks = doc["networks"].to<JsonArray>();
  const int count = WiFi.scanNetworks();
  if (count < 0) {
    return;
  }

  for (int i = 0; i < count; ++i) {
    JsonObject network = networks.add<JsonObject>();
    network["ssid"] = WiFi.SSID(i);
    network["rssi"] = WiFi.RSSI(i);
    network["secure"] = isSecureNetwork(WiFi.encryptionType(i));
  }
  WiFi.scanDelete();
}

bool WifiConnectionManager::connectAndSave(const String& ssid, const String& password) {
  if (ssid.isEmpty()) {
    return false;
  }

  WifiConfig next = configManager.loadWifiConfig();
  next.staSsid = ssid;
  next.staPassword = password;
  configManager.saveWifiConfig(next);
  config_ = next;
  return true;
}

bool WifiConnectionManager::tryStationConnect(const String& ssid, const String& password) {
  LOG_PRINTF("Connecting to STA \"%s\"...\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  const uint32_t deadline = millis() + kStationConnectTimeoutMs;
  while (WiFi.status() != WL_CONNECTED && millis() < deadline) {
    delay(kStationConnectPollMs);
    yield();
  }

  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  LOG_PRINTF("STA connect failed, status=%d\n", static_cast<int>(WiFi.status()));
  WiFi.disconnect();
  return false;
}

void WifiConnectionManager::startAccessPoint() {
  mode_ = WifiMode::kAccessPoint;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(config_.apSsid.c_str(), config_.apPassword.c_str());

  while (WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
    delay(10);
    yield();
  }

  LOG_PRINTF("AP \"%s\" started  IP: %s\n",
             config_.apSsid.c_str(),
             WiFi.softAPIP().toString().c_str());

  apStationConnectedHandler =
      WiFi.onSoftAPModeStationConnected(logAccessPointStationConnected);
}

WifiConnectionManager wifiConnectionManager;
