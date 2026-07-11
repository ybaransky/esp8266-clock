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
  uint8_t clockFmt;
  uint8_t brightness;
  bool clockUse12Hour;
};

struct CountdownConfig {
  char end[20];  // "YYYY-MM-DD HH:MM:SS"
  uint8_t format;
};

struct CountupConfig {
  char start[20];  // "YYYY-MM-DD HH:MM:SS" or "now"
  uint8_t format;
};

struct FridayConfig {
  uint8_t clockFmt;             // Clock phase (Sun midnight through Fri midnight).
  uint8_t toFridaySunsetFmt;    // Friday-midnight to Friday-sunset countdown.
  uint8_t toSaturdaySunsetFmt;  // Friday-sunset to Saturday-sunset countdown.
};

struct LocationConfig {
  LocationInfo device;      // Physical clock location used by Friday mode.
  LocationInfo sunsetTest;  // Independent Sunset Calculator test input.
};

struct MessageConfig {
  char splash[64];        // Startup message shown on the displays.
  char final[64];         // Message shown when countdown reaches zero.
  char fridaySunset[64];  // Blinked when Friday sunset is crossed live.
};

struct TimezoneConfig {
  char name[40];
  int16_t utcOffsetMinutes;
};

// -- ClockConfig ---------------------------------------------------------------
// Holds the user's display configuration. Persisted to / loaded from config.json.
struct ClockConfig {
  Mode activeMode;  // Persistent mode restored after any temporary overlay.

  FridayConfig friday;

  MessageConfig messages;

  LocationConfig locations;

  TimezoneConfig timezone;
  bool dst;  // Persisted browser DST flag; numeric offset drives sunset math.
  DisplayConfig display;
  CountdownConfig countdown;
  CountupConfig countup;
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
