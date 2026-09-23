#pragma once
#include "config.h"

// Centralizes the default values backing config.json: what a fresh
// ClockConfig/WifiConfig look like on first boot.

// Fills `out` with the factory defaults.
//
// Deliberately fills a caller-provided object rather than returning one. A
// ~700-byte ClockConfig is too big to hand around on the ESP8266: returning by
// value stacked copies on the 4KB cont stack, and caching a single static
// instance to return by reference cost the same 700 bytes of static RAM
// permanently, against a budget the project holds under 50% for OTA headroom.
// The only caller that needs a whole default config already owns one to fill.
//
// Code that needs a single default value uses the accessors below instead, so
// nothing has to materialize a full config just to read one fallback.
void initDefaultClockConfig(ClockConfig& out);

// Individual defaults, for validation paths that need one fallback rather than
// a whole config. These are the pieces sanitizeClockConfig() consults.
Mode defaultActiveMode();
const char* defaultCountdownEnd();
const char* defaultCountupStart();
TradingSchedule defaultTradingSchedule();
// Default format keys, resolved to indexes by the format sanitizers. Named by
// key rather than index for the reason described in defaults.cpp.
const char* defaultClockFormatKey();
const char* defaultCountingFormatKey();

// Returns a WifiConfig initialised to sensible defaults (empty station
// credentials; fallback access-point SSID/password).
WifiConfig defaultWifiConfig();
