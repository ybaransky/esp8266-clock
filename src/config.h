#pragma once
#include <Arduino.h>

#include "schedule.h"

static constexpr int32_t kForever = INT32_MAX;

// Sentinel for optional secondary format indexes: use the primary format.
static constexpr uint8_t kSameFormat = 0xFF;

// Size of every stored sound name, including the terminator. A sound's name is
// its identity - in the catalog, in /config.json, and in the API - so this one
// value bounds all three. Must match MAX_NAME_BYTES (this value minus one) in
// tools/pack_songs.py, which fails the build rather than let a name be silently
// truncated into one that matches nothing.
static constexpr size_t kSoundNameLength = 48;

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
    String apSsid;       // SSID for fallback AP mode; empty = derive ESP_XXXXXX from the soft-AP MAC.
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

// Stores the format selected for each phase of the Friday schedule, plus the
// two blink windows that bracket Friday sunset.
struct FridayConfig {
  uint8_t clockFmt;             // Clock phase (Saturday sunset through Friday midnight).
  uint8_t toFridaySunsetFmt;    // Friday-midnight to Friday-sunset countdown.
  uint8_t toSaturdaySunsetFmt;  // Friday-sunset to Saturday-sunset countdown.
  uint8_t blinkBeforeMinutes;   // Blink for this many minutes before Friday sunset; 0 = off.
  uint8_t blinkAfterMinutes;    // Blink for this many minutes after Friday sunset; 0 = off.
};

// Stores Trading-mode presentation and its local-time session schedule.
struct TradingConfig {
  uint8_t format;        // Selected counting-format index.
  uint8_t formatOver24;  // Format while >= 24h remain; kSameFormat = use format.
  TradingSchedule schedule;  // Enabled count plus both retained session slots.
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
  char tradingOpen[64];   // Blinked when a Trading session starts live.
  char tradingClose[64];  // Blinked when a Trading session stops live.
};

// Stores the buzzer settings and the sound played at each announced boundary.
// The name fields mirror MessageConfig one-for-one: every event that blinks a
// message can also play a sound, and an empty name means that event is silent.
struct SoundConfig {
  bool enabled;             // Master switch; false silences every event cue.
  uint8_t volumePercent;    // Loudness from 0 through 100.
  char startup[kSoundNameLength];       // Played once at boot, under the splash.
  char final[kSoundNameLength];         // Played when a countdown reaches zero.
  char fridaySunset[kSoundNameLength];  // Played when Friday sunset is crossed live.
  char tradingOpen[kSoundNameLength];   // Played when a Trading session starts live.
  char tradingClose[kSoundNameLength];  // Played when a Trading session stops live.
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
  SoundConfig sound;  // Buzzer settings and per-event sound selections.
  LocationConfig locations;  // Device and sunset-test coordinates.
  TimezoneConfig timezone;  // Local timezone and UTC offset.
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
    // Sanitizes cfg in place before persisting, so the caller's copy always
    // matches what was written to disk - no separate re-sanitize step needed.
    bool        saveClockConfig(ClockConfig& cfg);
    bool        saveConfig(ClockConfig& clock, const WifiConfig& wifi);
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
