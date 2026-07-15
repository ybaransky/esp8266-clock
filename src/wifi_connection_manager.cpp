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
WiFiEventHandler apStationDisconnectedHandler;
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

void onApStationDisconnected(const WiFiEventSoftAPModeStationDisconnected& event) {
  // Same deferral rule as onApStationConnected.
  if (gInstance) {
    gInstance->onApClientDisconnected(event.mac);
  }
}

void formatMac(const uint8_t mac[6], char out[18]) {
  snprintf(out, 18, "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
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

  if (!config_.staSsid.isEmpty() &&
      tryStationConnect(config_.staSsid, config_.staPassword)) {
    mode_ = WifiMode::kStation;
    LOG_PRINTF("STA \"%s\" connected  IP: %s",
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

void WifiConnectionManager::onApClientDisconnected(const uint8_t* mac) {
  memcpy(apDisconnectedMac_, mac, 6);
  apClientDisconnectedPending_ = true;
}

void WifiConnectionManager::tick() {
  if (apClientDisconnectedPending_) {
    apClientDisconnectedPending_ = false;
    char mac[18];
    formatMac(apDisconnectedMac_, mac);
    // The station is already gone from the SDK's list, so its IP can only
    // come from the mapping remembered at connect time.
    if ((memcmp(apDisconnectedMac_, lastClientMac_, sizeof(lastClientMac_)) == 0) &&
        lastClientIp_.isSet()) {
      LOG_PRINTF("AP client disconnected  IP: %s  MAC: %s",
                 lastClientIp_.toString().c_str(), mac);
    } else {
      LOG_PRINTF("AP client disconnected  MAC: %s", mac);
    }
  }

  if (!apClientConnectedPending_) return;

  station_info* station = wifi_softap_get_station_info();
  for (station_info* current = station; current != nullptr;
       current = STAILQ_NEXT(current, next)) {
    if (memcmp(current->bssid, apClientMac_, sizeof(apClientMac_)) != 0) continue;
    const IPAddress ip(current->ip);
    if (ip != IPAddress(0, 0, 0, 0)) {
      LOG_PRINTF("AP client connected  IP: %s", ip.toString().c_str());
      memcpy(lastClientMac_, apClientMac_, sizeof(lastClientMac_));
      lastClientIp_ = ip;
      apClientConnectedPending_ = false;
    }
    break;
  }
  wifi_softap_free_station_info();

  if (apClientConnectedPending_ &&
      (static_cast<uint32_t>(millis() - apClientLookupStartedMs_) >=
       kApClientIpTimeoutMs)) {
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
  LOG_PRINTF("Connecting to STA \"%s\"...", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());

  const uint32_t deadline = millis() + kStationConnectTimeoutMs;
  while ((WiFi.status() != WL_CONNECTED) && (millis() < deadline)) {
    delay(kStationConnectPollMs);
    yield();
  }

  if (WiFi.status() == WL_CONNECTED) {
    return true;
  }

  LOG_PRINTF("STA connect failed, status=%d", static_cast<int>(WiFi.status()));
  WiFi.disconnect();
  return false;
}

// softAP() defaults to channel 1, the most crowded 2.4 GHz channel. With the
// AP on channel 1 (2026-07-13), a page load showed loss in every phase --
// conn:1023 (one SYN retransmit) and dl:4612 for a ~2 KB body -- so survey
// the air once and start the AP on the quietest primary channel. The logged
// scores double as RF-environment evidence.
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
  LOG_PRINTF("AP channel survey (%d networks): ch1=%ld ch6=%ld ch11=%ld -> channel %u",
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
  WiFi.mode(WIFI_AP);
  // Confirmed on stock settings (2026-07-13): transfers to a power-save
  // client stall mid-body (TRUNCATED wrote 906 of 1236 bytes, healthy heap).
  // The ESP8266's 11n softAP path is the known-bad case when buffering for
  // dozing clients; forcing 802.11g avoids it. Reintroduce further tweaks
  // one at a time, only against observed evidence.
  WiFi.setPhyMode(WIFI_PHY_MODE_11G);
  // Keep the default 100 TU beacon interval: 50 TU was tried against the
  // power-save Android client (2026-07-13) and produced frequent TRUNCATED
  // transfers instead of helping.
  WiFi.softAP(config_.apSsid.c_str(), config_.apPassword.c_str(), channel);
  // Full power (20.5 dBm) draws supply-drooping TX spikes over USB; long
  // frames (multi-segment page bodies) are the first casualties, and supply
  // sag varies run to run -- matching the observed inconsistency between
  // otherwise-identical test sessions (2026-07-13).
  WiFi.setOutputPower(17.0f);

  while (WiFi.softAPIP() == IPAddress(0, 0, 0, 0)) {
    delay(10);
    yield();
  }

  LOG_PRINTF("AP \"%s\" started  IP: %s",
             config_.apSsid.c_str(),
             WiFi.softAPIP().toString().c_str());

  apStationConnectedHandler =
      WiFi.onSoftAPModeStationConnected(onApStationConnected);
  apStationDisconnectedHandler =
      WiFi.onSoftAPModeStationDisconnected(onApStationDisconnected);
}
