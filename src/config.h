#pragma once
#include <Arduino.h>
#include "format.h"

#define STORAGE LittleFS

static constexpr int32_t FOREVER = INT32_MAX;

// -- WifiConfig ----------------------------------------------------------------
struct WifiConfig {
    String staSsid;      // SSID used when joining an existing WiFi network.
    String staPassword;  // Password for staSsid.
    String apSsid;       // SSID advertised by fallback access-point mode.
    String apPassword;   // Password for fallback access-point mode.
};

// Geographic location used by both the device and sunset calculator inputs.
struct LocationInfo {
  float latitude  = 0.0f;
  float longitude = 0.0f;
  char  zipcode[6] = {};
};

// -- ClockConfig ---------------------------------------------------------------
// Holds the user's display configuration. Persisted to / loaded from config.json.
struct ClockConfig {
  PersistentMode activeMode;  // Persistent mode restored after temporary states.

  uint8_t countdownFmt;  // index into kCountdownFormats
  uint8_t countupFmt;    // index into kCountupFormats
  uint8_t clockFmt;      // index into kClockFormats
  uint8_t brightness;    // TM1637 brightness 0-7

  uint8_t fridayClockFmt;          // kFmtGroupClock index for clock phase (Sun midnight – Fri midnight)
  uint8_t fridayToFridaySunsetFmt; // kFmtGroupCountdown index for Fri midnight – Fri sunset
  uint8_t fridayToSatSunsetFmt;    // kFmtGroupCountdown index for Fri sunset – Sat sunset

  char countdownDatetime[20]; // "YYYY-MM-DD HH:MM:SS"
  char countupDatetime[20];   // "YYYY-MM-DD HH:MM:SS" or "now"

  char splashMessage[64];  // Startup message shown on the displays.
  char finalMessage[64];   // Message shown when countdown reaches zero.

  LocationInfo location;    // Physical device location.
  LocationInfo sunsetTest;  // Sunset calculator test inputs (distinct from device location).

  char timezone[40];          // IANA timezone, e.g. "America/New_York".
  int16_t utcOffsetMinutes;   // Current browser offset fallback.
  bool dst;                    // True when daylight saving time is active.
  bool clockUse12Hour;         // True for 12-hour display (1–12); false for 24-hour (0–23).
};

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
