#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <IPAddress.h>

#include "config.h"

enum class WifiMode {
  kStation,
  kAccessPoint,
};

// Snapshots the connection details exposed to displays and HTTP clients.
struct WifiRuntimeStatus {
  WifiMode mode;    // Active WiFi mode.
  bool connected;   // True when station mode has an active connection.
  String ssid;      // Connected station SSID.
  String ip;        // Station IP address.
  String apSsid;    // Access-point SSID.
  String apIp;      // Access-point IP address.
};

// Manages station connection attempts, AP fallback, scans, and deferred client events.
class WifiConnectionManager {
 public:
  void begin(const WifiConfig& config);
  void tick();
  void onApClientConnected(const uint8_t* mac);
  void onApClientDisconnected(const uint8_t* mac);

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
  uint8_t apClientMac_[6] = {};  // MAC used to find the DHCP-assigned client IP.
  uint32_t apClientLookupStartedMs_ = 0;  // Start time for deferred DHCP lookup.

  volatile bool apClientDisconnectedPending_ = false;  // Deferred disconnect log flag.
  uint8_t apDisconnectedMac_[6] = {};  // MAC reported by the disconnect event.
  uint8_t lastClientMac_[6] = {};  // MAC of the most recently connected client.
  IPAddress lastClientIp_;  // Its DHCP IP; names the client in the disconnect log.
};
