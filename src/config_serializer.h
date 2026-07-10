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

// Applies every clock-config field present in root onto cfg (patch semantics:
// absent fields are untouched). Used both to load config.json (base = defaults)
// and to apply a POST /api/config payload (base = loaded config). Returns
// nullptr on success, or a static error-JSON string for the first invalid value
// - in that case cfg may be partially updated and should be discarded.
const char* applyJsonToClockConfig(JsonVariantConst root, ClockConfig& cfg);

// Same patch semantics for the wifi section. Returns true when any wifi field
// was present (callers use this to decide whether a reboot is needed).
bool applyJsonToWifiConfig(JsonVariantConst root, WifiConfig& wifi);
