#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "config.h"

enum class WifiMode {
  kStation,
  kAccessPoint,
};

struct WifiRuntimeStatus {
  WifiMode mode;    // Active WiFi mode.
  bool connected;   // True when station mode has an active connection.
  String ssid;      // Connected station SSID.
  String ip;        // Station IP address.
  String apSsid;    // Access-point SSID.
  String apIp;      // Access-point IP address.
};

class WifiConnectionManager {
 public:
  void begin(const WifiConfig& config);
  void tick();
  void onApClientConnected(const uint8_t* mac);

  WifiRuntimeStatus status() const;
  void scanNetworks(JsonDocument& doc);
  bool connectAndSave(ConfigManager& configManager,
                      const String& ssid,
                      const String& password);

 private:
  bool tryStationConnect(const String& ssid, const String& password);
  void startAccessPoint();
  uint8_t pickLeastCongestedChannel();

  WifiConfig config_;  // Last loaded WiFi configuration.
  WifiMode mode_ = WifiMode::kAccessPoint;  // Current runtime WiFi mode.

  volatile bool apClientConnectedPending_ = false;  // Deferred AP-client log flag.
  uint8_t apClientMac_[6] = {}; // Used internally to find the DHCP-assigned IP.
  uint32_t apClientLookupStartedMs_ = 0;
};
