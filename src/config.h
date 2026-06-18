#pragma once
#include <Arduino.h>
#include "format.h"

#define STORAGE LittleFS

static constexpr int32_t FOREVER = INT32_MAX;

// -- WifiConfig ----------------------------------------------------------------
struct WifiConfig {
    String staSsid;
    String staPassword;
    String apSsid;
    String apPassword;
};

// -- ClockConfig ---------------------------------------------------------------
// Holds the user's display configuration. Persisted to / loaded from config.json.
struct ClockConfig {
  PersistentMode activeMode;  // Persistent mode restored after temporary states.

  uint8_t countdownFmt;  // index into kCountdownFormats
  uint8_t countupFmt;    // index into kCountupFormats
  uint8_t clockFmt;      // index into kClockFormats
  uint8_t brightness;    // TM1637 brightness 0-7

  char countdownDatetime[20]; // "YYYY-MM-DD HH:MM:SS"
  char countupDatetime[20];   // "YYYY-MM-DD HH:MM:SS" or "now"

  char splashMessage[64];
  char finalMessage[64];

  float latitude;
  float longitude;
  char zipcode[6];
  char timezone[40];          // IANA timezone, e.g. "America/New_York".
  int16_t utcOffsetMinutes;   // Current browser offset fallback.
};

// Returns a ClockConfig initialised to sensible defaults.
ClockConfig defaultClockConfig();

// -- ConfigManager -------------------------------------------------------------
class ConfigManager {
public:
    WifiConfig  loadWifiConfig();
    void        saveWifiConfig(const WifiConfig& cfg);

    ClockConfig loadClockConfig();
    void        saveClockConfig(const ClockConfig& cfg);
    ClockConfig sanitizeClockConfig(const ClockConfig& cfg) const;

};

extern ConfigManager configManager;
