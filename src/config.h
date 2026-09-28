#pragma once
#include <Arduino.h>

#include "schedule.h"
#include "beep_pattern.h"
#include "datetime_validation.h"

// Sentinel for optional secondary format indexes: use the primary format.
static constexpr uint8_t kSameFormat = 0xFF;

// Shared bound for persisted and rendered display messages, including NUL.
static constexpr size_t kDisplayMessageLength = 64;

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
  char zipcode[6] = {};    // Five-digit ZIP code plus terminator.
};

// Stores display presentation settings used by the clock renderer and hardware.
struct DisplayConfig {
  uint8_t clockFormat = 0;      // Clock-format index used by Clock mode.
  uint8_t brightness = 3;       // TM1637 brightness level from 0 through 7.
  bool clockUse12Hour = false;  // True to render clock hours on a 12-hour scale.
};

// Stores the target and renderer selection for countdown mode.
struct CountdownConfig {
  char end[kLocalDateTimeLength] = {};  // Local target, "YYYY-MM-DD HH:MM:SS".
  uint8_t format = 0;                   // Counting-format index.
};

// Sentinel stored in CountupConfig::start meaning "never been set": resolve it
// against the RTC. It exists because no absolute datetime is a correct factory
// default - a shipped date would make a fresh device show an ever-growing
// elapsed time, the way the shipped CountdownConfig::end goes stale. It is
// substituted for a concrete datetime by resolveCountupStart() on the first
// save, so it reaches the display path only on a device that has never saved.
static constexpr char kCountupStartNow[] = "now";

// Stores the origin and renderer selection for count-up mode.
struct CountupConfig {
  char start[kLocalDateTimeLength] = {};  // Local origin, "YYYY-MM-DD HH:MM:SS", or kCountupStartNow when unset.
  uint8_t format = 0;                     // Counting-format index.
};

// Stores the format selected for each phase of the Friday schedule, plus the
// two blink windows that bracket Friday sunset.
struct FridayConfig {
  uint8_t clockFormat = 0;             // Clock-format index, Saturday sunset to Friday midnight.
  uint8_t toFridaySunsetFormat = 0;    // Counting-format index, Friday midnight to Friday sunset.
  uint8_t toSaturdaySunsetFormat = 0;  // Counting-format index, Friday sunset to Saturday sunset.
  uint8_t blinkBeforeMinutes = 0;      // Blink for this many minutes before Friday sunset; 0 = off.
  uint8_t blinkAfterMinutes = 0;       // Blink for this many minutes after Friday sunset; 0 = off.
};

// Stores Trading-mode presentation and its local-time session schedule.
struct TradingConfig {
  uint8_t format = 0;                  // Counting-format index.
  uint8_t formatOver24 = kSameFormat;  // Counting-format index while >= 24h remain; kSameFormat = use format.
  TradingSchedule schedule;            // Enabled count plus both retained session slots.
};

// Keeps the physical device location separate from sunset-page test input.
struct LocationConfig {
  LocationInfo device;      // Physical clock location used by Friday mode.
  LocationInfo sunsetTest;  // Independent Sunset Calculator test input.
};

// Stores configurable text shown by startup, completion, and scheduled overlays.
struct MessageConfig {
  char splash[kDisplayMessageLength] = {};         // Startup message shown on the displays.
  char countdownDone[kDisplayMessageLength] = {};  // Shown when a countdown reaches zero; JSON key "final".
  char fridaySunset[kDisplayMessageLength] = {};   // Blinked when Friday sunset is crossed live.
  char tradingOpen[kDisplayMessageLength] = {};    // Blinked when a Trading session starts live.
  char tradingClose[kDisplayMessageLength] = {};   // Blinked when a Trading session stops live.
};

// Stores generated-beep settings: master switch, volume, event beeps, and approach patterns.
struct SoundConfig {
  bool enabled = true;            // Master switch for automatic beeps and approach alerts.
  uint8_t volumePercent = 40;     // PWM loudness from 0 through 100.
  bool startupBeep = false;       // Short beep once startup has finished.
  bool finalBeep = false;         // Short beep at ordinary countdown completion.
  bool fridaySunsetBeep = false;  // Short beep on a live Friday-sunset crossing.
  bool tradingOpenBeep = false;   // Short beep on a live session open.
  bool tradingCloseBeep = false;  // Short beep on a live session close.

  // Generated alerts that finish at scheduled mode boundaries.
  struct BoundaryAlertConfig {
    bool enabled = true;    // Enables both generated pre-boundary patterns.
    BeepPattern boundary1;  // Approach alert for Friday sunset and the first Trading open.
    BeepPattern boundary2;  // Approach alert for Saturday sunset and the last Trading close.
  } boundaryAlert;          // Accelerating alerts that end exactly at a boundary.
};

// Stores the local timezone identity and the numeric offset used by sunset math.
struct TimezoneConfig {
  char name[40] = {};            // IANA timezone name supplied by the browser.
  int16_t utcOffsetMinutes = 0;  // Current local offset from UTC in minutes.
};

// Aggregates all persisted clock behavior and presentation settings.
struct ClockConfig {
  Mode activeMode = kModeClock;  // Persistent mode restored after any temporary overlay.
  FridayConfig friday;           // Friday-mode phase formats and sunset blink windows.
  TradingConfig trading;         // Trading-mode formats and session schedule.
  MessageConfig messages;        // User-configurable display messages.
  SoundConfig sound;             // Generated approach alerts and optional event beeps.
  LocationConfig locations;      // Device and sunset-test coordinates.
  TimezoneConfig timezone;       // Local timezone and UTC offset.
  DisplayConfig display;         // Clock rendering and hardware brightness settings.
  CountdownConfig countdown;     // Countdown target and format.
  CountupConfig countup;         // Count-up origin and format.
};

// Groups both persisted configuration domains for complete file serialization.
struct DeviceConfig {
  ClockConfig clock;  // Clock configuration section.
  WifiConfig wifi;    // WiFi configuration section.
};

// ClockConfig is copied onto the ESP8266's 4KB cont stack by any handler that
// takes one by value, so its size is a budget, not a detail. An assert enforces
// it because a size stated only in a comment goes stale unnoticed.
static_assert(sizeof(ClockConfig) <= 768,
              "ClockConfig grew past its stack budget; see ConfigManager");

// Owns cached configuration and persists sanitized updates with backup recovery.
class ConfigManager {
public:
    // Both accessors return the cached configuration by reference. The cache
    // outlives every caller, and handing out a ~700-byte copy per call was
    // stacking several of them at once on the 4KB cont stack during a save.
    // Callers that need to modify a config copy it explicitly.
    const WifiConfig&  wifiConfig();
    const ClockConfig& clockConfig();

    bool        saveWifiConfig(const WifiConfig& config);
    // Sanitizes config in place before persisting, so the caller's copy always
    // matches what was written to disk - no separate re-sanitize step needed.
    bool        saveClockConfig(ClockConfig& config);
    bool        saveConfig(ClockConfig& clock, const WifiConfig& wifi);
    // Sanitizes config in place, for the same stack reason as the accessors above.
    void        sanitizeClockConfig(ClockConfig& config) const;

private:
    bool ensureLoaded();
    bool readAll(DeviceConfig& config);
    // Writes config to the temp file and confirms it parses back. Split from
    // installVerifiedTemp() so the serialization document is destroyed before
    // the verification document is built, rather than both being live at once.
    bool serializeToTemp(const DeviceConfig& config, const char* context,
                         size_t& bytes);
    bool verifyTemp(const char* context);
    bool installVerifiedTemp(const char* context);
    bool writeAll(const DeviceConfig& config, const char* context);

    DeviceConfig m_current;  // Cached configuration loaded from storage.
    bool m_loaded = false;   // True after m_current has been initialized.
};
