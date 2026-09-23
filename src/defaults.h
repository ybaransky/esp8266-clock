#pragma once
#include "config.h"

// Centralizes the default values backing config.json: what a fresh
// ClockConfig/WifiConfig look like on first boot.

// Returns a ClockConfig initialised to sensible defaults.
ClockConfig defaultClockConfig();

// Default format keys, resolved to indexes by the format sanitizers. Named by
// key rather than index for the reason described in defaults.cpp.
const char* defaultClockFormatKey();
const char* defaultCountingFormatKey();

// Returns a WifiConfig initialised to sensible defaults (empty station
// credentials; fallback access-point SSID/password).
WifiConfig defaultWifiConfig();
