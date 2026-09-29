#pragma once

#include <ArduinoJson.h>

struct ClockConfig;
struct WifiConfig;

// Schema version written into /config.json as "configVersion". Bump this only
// for a change that older firmware cannot read correctly; adding a field with
// patch semantics does not need it. Format selections are stored as stable
// keys rather than table indexes, so reordering the format catalog is not a
// schema change.
//
// Version 2: the DS3231 holds UTC rather than local time, and the timezone is
// stored as a POSIX rule. Firmware reading a version-1 file converts the RTC
// once (ClockApplication::migrateRtcToUtcIfNeeded).
static constexpr uint8_t kConfigSchemaVersion = 2;

// Writes the clock/display/time/location/sunset sections of the config JSON document.
void serializeClockConfig(JsonDocument& doc, const ClockConfig& config);

// Writes the full wifi section including both passwords (for on-disk storage).
void serializeWifiConfig(JsonDocument& doc, const WifiConfig& wifi);

// Writes only the wifi SSIDs, omitting the station password (for HTTP API responses).
void serializeWifiStatus(JsonDocument& doc, const WifiConfig& wifi);

// Clamps every format-index field to a valid value for its format group,
// falling back to that group's default format. Shares the field list
// (mode/JSON key, target member, format group) with applyJsonToClockConfig
// so the two directions can't drift apart.
void sanitizeFormatFields(ClockConfig& config);

// Re-sanitizes every display message field in place (trims to printable
// ASCII, clamps length). Shares the field list with applyJsonToClockConfig.
void sanitizeMessageFields(ClockConfig& config);

// Clamps volume and generated boundary-pattern settings to supported ranges.
void sanitizeSoundFields(ClockConfig& config);

// Applies every clock-config field present in root onto config (patch semantics:
// absent fields are untouched). Used both to load config.json (base = defaults)
// and to apply a POST /api/config payload (base = loaded config). Returns
// nullptr on success, or a static error-JSON string for the first invalid value
// - in that case config may be partially updated and should be discarded.
const char* applyJsonToClockConfig(JsonVariantConst root, ClockConfig& config);

// Same patch semantics for the wifi section. Returns true when any wifi field
// was present (callers use this to decide whether a reboot is needed).
bool applyJsonToWifiConfig(JsonVariantConst root, WifiConfig& wifi);

// The "configVersion" a loaded document was written with; 1 when absent.
uint8_t readConfigSchemaVersion(JsonVariantConst root);
