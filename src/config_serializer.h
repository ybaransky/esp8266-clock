#pragma once

#include <ArduinoJson.h>

struct ClockConfig;
struct WifiConfig;

// Writes the clock/display/time/location/sunset sections of the config JSON document.
void serializeClockConfig(JsonDocument& doc, const ClockConfig& config);

// Writes the full wifi section including both passwords (for on-disk storage).
void serializeWifiConfig(JsonDocument& doc, const WifiConfig& wifi);

// Writes only the wifi SSIDs, omitting the station password (for HTTP API responses).
void serializeWifiStatus(JsonDocument& doc, const WifiConfig& wifi);
