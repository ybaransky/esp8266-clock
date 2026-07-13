#include "wifi_connection_manager.h"

#include <ESP8266WiFi.h>
extern "C" {
#include <user_interface.h>
}

#include "log.h"

namespace {

constexpr uint32_t kStationConnectTimeoutMs = 15000;
constexpr uint16_t kStationConnectPollMs = 250;
constexpr uint32_t kApClientIpTimeoutMs = 10000;

WiFiEventHandler apStationConnectedHandler;
WifiConnectionManager* gInstance = nullptr;

bool isSecureNetwork(uint8_t encryptionType) {
  return encryptionType != ENC_TYPE_NONE;
}

void onApStationConnected(const WiFiEventSoftAPModeStationConnected& event) {
  // I2C (used by LOG_PRINTF via logCurrentTime) is unsafe in WiFi event
  // callbacks - defer to tick() which runs from loop().
  if (gInstance) {
    gInstance->onApClientConnected(event.mac);
  }
}

}  // namespace

// -----------------------------------------------------------------------------
// WifiConnectionManager
// -----------------------------------------------------------------------------

void WifiConnectionManager::begin(const WifiConfig& config) {
  gInstance = this;
  config_ = config;
  WiFi.persistent(false);
  WiFi.setAutoReconnect(true);
  // This device is continuously powered and serves an interactive web UI.
  // Modem sleep can add noticeable latency between page requests, especially
  // after the browser has been idle, so keep the radio awake while running.
  WiFi.setSleepMode(WIFI_NONE_SLEEP);

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

void WifiConnectionManager::onApClientConnected(const uint8_t* mac) {
  memcpy(apClientMac_, mac, 6);
  apClientLookupStartedMs_ = millis();
  apClientConnectedPending_ = true;
}

void WifiConnectionManager::tick() {
  if (!apClientConnectedPending_) return;

  station_info* station = wifi_softap_get_station_info();
  for (station_info* current = station; current != nullptr;
       current = STAILQ_NEXT(current, next)) {
    if (memcmp(current->bssid, apClientMac_, sizeof(apClientMac_)) != 0) continue;
    const IPAddress ip(current->ip);
    if (ip != IPAddress(0, 0, 0, 0)) {
      LOG_PRINTF("AP client connected  IP: %s\n", ip.toString().c_str());
      apClientConnectedPending_ = false;
    }
    break;
  }
  wifi_softap_free_station_info();

  if (apClientConnectedPending_ &&
      static_cast<uint32_t>(millis() - apClientLookupStartedMs_) >=
          kApClientIpTimeoutMs) {
    LOG_PRINTLN("AP client connected but no IP address was assigned");
    apClientConnectedPending_ = false;
  }
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
  const bool restoreApOnly = mode_ == WifiMode::kAccessPoint;
  if (restoreApOnly) {
    WiFi.mode(WIFI_AP_STA);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
  }
  JsonArray networks = doc["networks"].to<JsonArray>();
  const int count = WiFi.scanNetworks();
  if (count < 0) {
    if (restoreApOnly) WiFi.mode(WIFI_AP);
    return;
  }

  for (int i = 0; i < count; ++i) {
    JsonObject network = networks.add<JsonObject>();
    network["ssid"] = WiFi.SSID(i);
    network["rssi"] = WiFi.RSSI(i);
    network["secure"] = isSecureNetwork(WiFi.encryptionType(i));
  }
  WiFi.scanDelete();
  if (restoreApOnly) {
    WiFi.mode(WIFI_AP);
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
  }
}

bool WifiConnectionManager::connectAndSave(ConfigManager& configManager,
                                           const String& ssid,
                                           const String& password) {
  if (ssid.isEmpty()) {
    return false;
  }

  WifiConfig next = configManager.loadWifiConfig();
  next.staSsid = ssid;
  next.staPassword = password;
  if (!configManager.saveWifiConfig(next)) {
    LOG_PRINTLN("WiFi connect save failed: complete config write failed");
    return false;
  }
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
    WiFi.setSleepMode(WIFI_NONE_SLEEP);
    return true;
  }

  LOG_PRINTF("STA connect failed, status=%d\n", static_cast<int>(WiFi.status()));
  WiFi.disconnect();
  return false;
}

// softAP() defaults to channel 1, the most crowded 2.4 GHz channel. Neighbor
// traffic there causes packet loss that stalls page transfers mid-body, so
// survey the air once and start the AP on the quietest primary channel.
uint8_t WifiConnectionManager::pickLeastCongestedChannel() {
  const uint8_t candidates[] = {1, 6, 11};
  int32_t scores[3] = {0, 0, 0};
  const int count = WiFi.scanNetworks();
  if (count <= 0) {
    return 1;
  }
  for (int i = 0; i < count; ++i) {
    const int32_t channel = WiFi.channel(i);
    int32_t rssi = WiFi.RSSI(i);
    if (rssi < -100) rssi = -100;
    for (size_t c = 0; c < 3; ++c) {
      // A 20 MHz transmission bleeds into channels within +/-4; stronger
      // neighbors contribute more congestion.
      if (abs(channel - candidates[c]) <= 4) {
        scores[c] += 100 + rssi;
      }
    }
  }
  WiFi.scanDelete();
  size_t best = 0;
  for (size_t c = 1; c < 3; ++c) {
    if (scores[c] < scores[best]) best = c;
  }
  LOG_PRINTF("AP channel survey (%d networks): ch1=%ld ch6=%ld ch11=%ld -> channel %u\n",
             count,
             static_cast<long>(scores[0]),
             static_cast<long>(scores[1]),
             static_cast<long>(scores[2]),
             candidates[best]);
  return candidates[best];
}

void WifiConnectionManager::startAccessPoint() {
  mode_ = WifiMode::kAccessPoint;
  const uint8_t channel = pickLeastCongestedChannel();
  // A disconnected station interface may scan/reconnect in the background,
  // stealing radio time from the local web server. The fallback portal needs
  // only AP mode; STA is enabled temporarily by scanNetworks().
  WiFi.setAutoReconnect(false);
  WiFi.disconnect();
  WiFi.mode(WIFI_AP);
  // Phones keep 802.11 power-save enabled regardless of their user-facing
  // battery settings, and the ESP8266's 11n softAP path stalls mid-transfer
  // when buffering for power-save clients. Forcing 802.11g avoids that path.
  WiFi.setPhyMode(WIFI_PHY_MODE_11G);
  WiFi.softAP(config_.apSsid.c_str(), config_.apPassword.c_str(), channel);
  WiFi.setSleepMode(WIFI_NONE_SLEEP);
  // Full power (20.5 dBm) draws supply-drooping TX spikes over USB and can
  // saturate a nearby client's receiver; 17 dBm is the stable choice.
  WiFi.setOutputPower(17.0f);

  while (WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
    delay(10);
    yield();
  }

  LOG_PRINTF("AP \"%s\" started  IP: %s\n",
             config_.apSsid.c_str(),
             WiFi.softAPIP().toString().c_str());

  apStationConnectedHandler =
      WiFi.onSoftAPModeStationConnected(onApStationConnected);
}
