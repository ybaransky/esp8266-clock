#pragma once
#include <Arduino.h>

#define STORAGE LittleFS

static constexpr int32_t kForever = INT32_MAX;

// Persistent setting selected by the user. This is distinct from the
// currently rendered View and any temporary Overlay (see display_manager.h).
enum Mode : uint8_t {
  kModeCountdown = 0,
  kModeCountup   = 1,
  kModeClock     = 2,
  kModeFriday    = 3,
  kModeTrading   = 4,
};

// Stores the station and fallback access-point credentials used to configure WiFi.
struct WifiConfig {
    String staSsid;      // SSID used when joining an existing WiFi network.
    String staPassword;  // Password for staSsid.
    String apSsid;       // SSID advertised by fallback access-point mode.
    String apPassword;   // Password for fallback access-point mode.
};

// Geographic location used by both the device and sunset calculator inputs.
struct LocationInfo {
  float latitude  = 0.0f;  // Latitude in decimal degrees.
  float longitude = 0.0f;  // Longitude in decimal degrees.
  char zipcode[6] = {};     // Five-digit ZIP code plus terminator.
};

// Stores display presentation settings used by the clock renderer and hardware.
struct DisplayConfig {
  uint8_t clockFmt;      // Selected clock-format index.
  uint8_t brightness;    // TM1637 brightness level from 0 through 7.
  bool clockUse12Hour;   // True to render clock hours on a 12-hour scale.
};

// Stores the target and renderer selection for countdown mode.
struct CountdownConfig {
  char end[20];  // "YYYY-MM-DD HH:MM:SS"
  uint8_t format;  // Selected counting-format index.
};

// Stores the origin and renderer selection for count-up mode.
struct CountupConfig {
  char start[20];  // "YYYY-MM-DD HH:MM:SS" or "now"
  uint8_t format;  // Selected counting-format index.
};

// Stores the format selected for each phase of the Friday schedule.
struct FridayConfig {
  uint8_t clockFmt;             // Clock phase (Sun midnight through Fri midnight).
  uint8_t toFridaySunsetFmt;    // Friday-midnight to Friday-sunset countdown.
  uint8_t toSaturdaySunsetFmt;  // Friday-sunset to Saturday-sunset countdown.
};

// Stores the counting format used for Trading-mode boundary countdowns.
struct TradingConfig {
  uint8_t format;  // Selected counting-format index.
};

// Keeps the physical device location separate from sunset-page test input.
struct LocationConfig {
  LocationInfo device;      // Physical clock location used by Friday mode.
  LocationInfo sunsetTest;  // Independent Sunset Calculator test input.
};

// Stores configurable text shown by startup, completion, and scheduled overlays.
struct MessageConfig {
  char splash[64];        // Startup message shown on the displays.
  char final[64];         // Message shown when countdown reaches zero.
  char fridaySunset[64];  // Blinked when Friday sunset is crossed live.
  char tradingOpen[64];   // Blinked when Trading mode reaches 09:30 live.
  char tradingClose[64];  // Blinked when Trading mode reaches 16:00 live.
};

// Stores the local timezone identity and the numeric offset used by sunset math.
struct TimezoneConfig {
  char name[40];             // IANA timezone name supplied by the browser.
  int16_t utcOffsetMinutes;  // Current local offset from UTC in minutes.
};

// Aggregates all persisted clock behavior and presentation settings.
struct ClockConfig {
  Mode activeMode;  // Persistent mode restored after any temporary overlay.
  FridayConfig friday;  // Friday-mode phase formats.
  TradingConfig trading;  // Trading-mode countdown format.
  MessageConfig messages;  // User-configurable display messages.
  LocationConfig locations;  // Device and sunset-test coordinates.
  TimezoneConfig timezone;  // Local timezone and UTC offset.
  bool dst;  // Persisted browser DST flag; numeric offset drives sunset math.
  DisplayConfig display;  // Clock rendering and hardware brightness settings.
  CountdownConfig countdown;  // Countdown target and format.
  CountupConfig countup;  // Count-up origin and format.
};

// Groups both persisted configuration domains for atomic file serialization.
struct DeviceConfig {
  ClockConfig clock;  // Clock configuration section.
  WifiConfig wifi;    // WiFi configuration section.
};

// Owns the cached device configuration and persists sanitized updates atomically.
class ConfigManager {
public:
    WifiConfig  loadWifiConfig();
    bool        saveWifiConfig(const WifiConfig& cfg);

    ClockConfig loadClockConfig();
    bool        saveClockConfig(const ClockConfig& cfg);
    bool        saveConfig(const ClockConfig& clock, const WifiConfig& wifi);
    // Sanitizes cfg in place. ClockConfig is ~450 bytes, and the web handlers
    // that save/apply configs run on the ESP8266's 4KB cont stack - returning
    // by value here stacked enough extra copies to overflow it.
    void        sanitizeClockConfig(ClockConfig& cfg) const;

private:
    bool ensureLoaded();
    bool readAll(DeviceConfig& config);
    bool writeAll(const DeviceConfig& config, const char* context);

    DeviceConfig current_;  // Cached configuration loaded from storage.
    bool loaded_ = false;   // True after current_ has been initialized.
};
