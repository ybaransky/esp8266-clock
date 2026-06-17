#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

#include "config.h"

enum class WifiMode {
  kStation,
  kAccessPoint,
};

struct WifiRuntimeStatus {
  WifiMode mode;
  bool connected;
  String ssid;
  String ip;
  String apSsid;
  String apIp;
};

class WifiConnectionManager {
 public:
  void begin(const WifiConfig& config);
  void tick();

  WifiRuntimeStatus status() const;
  void scanNetworks(JsonDocument& doc);
  bool connectAndSave(const String& ssid, const String& password);

 private:
  bool tryStationConnect(const String& ssid, const String& password);
  void startAccessPoint();

  WifiConfig config_;
  WifiMode mode_ = WifiMode::kAccessPoint;
};

extern WifiConnectionManager wifiConnectionManager;
