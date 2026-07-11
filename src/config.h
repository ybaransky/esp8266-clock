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

struct DisplayConfig {
  uint8_t countdownFmt;
  uint8_t countupFmt;
  uint8_t clockFmt;
  uint8_t brightness;
  bool clockUse12Hour;
};

struct FridayConfig {
  uint8_t clockFmt;             // Clock phase (Sun midnight through Fri midnight).
  uint8_t toFridaySunsetFmt;    // Friday-midnight to Friday-sunset countdown.
  uint8_t toSaturdaySunsetFmt;  // Friday-sunset to Saturday-sunset countdown.
};

// -- ClockConfig ---------------------------------------------------------------
// Holds the user's display configuration. Persisted to / loaded from config.json.
struct ClockConfig {
  Mode activeMode;  // Persistent mode restored after any temporary overlay.

  FridayConfig friday;

  char countdownDatetime[20]; // "YYYY-MM-DD HH:MM:SS"
  char countupDatetime[20];   // "YYYY-MM-DD HH:MM:SS" or "now"

  char splashMessage[64];       // Startup message shown on the displays.
  char finalMessage[64];        // Message shown when countdown reaches zero.
  char fridaySunsetMessage[64]; // Blinked for 5s when Friday mode crosses Friday sunset.

  LocationInfo location;    // Physical device location.
  LocationInfo sunsetTest;  // Sunset calculator test inputs (distinct from device location).

  char timezone[40];          // IANA timezone, e.g. "America/New_York".
  int16_t utcOffsetMinutes;   // Current browser offset fallback.
  bool dst;                    // True when daylight saving time is active.
  DisplayConfig display;
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
