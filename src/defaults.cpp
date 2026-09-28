#include "defaults.h"

#include "display_format.h"
#include "log.h"

namespace {

// Empty is a sentinel, not a missing value: WifiConnectionManager derives
// ESP_XXXXXX from the soft-AP MAC whenever the configured SSID is blank, so a
// freshly uploaded filesystem advertises the SDK-style name until the user
// picks one. Keep this in sync with data/config.json.
constexpr const char* kDefaultApSsid     = "";          // Blank: derive ESP_XXXXXX at startup.
constexpr const char* kDefaultApPassword = "12345678";  // Fallback AP password; WPA2 needs 8-63 characters.

// A placeholder, not a meaningful default: it is already in the past and cannot
// be otherwise, since any date compiled into firmware is stale by the time a
// device is powered on. It is reachable only when a user selects Countdown
// without setting an end time, because the shipped activeMode is Clock. Do not
// duplicate it in data/config.json - patch semantics make an absent field fall
// through to here, and the two copies had already drifted twelve weeks apart.
constexpr const char* kDefaultCountdownEnd = "2026-07-04 00:00:00";  // Placeholder countdown target.

// "Never set": resolved to the RTC's time on the first save.
constexpr const char* kDefaultCountupStart = kCountupStartNow;

// Leading spaces position the text across the three 4-character panels.
constexpr const char* kDefaultSplashMessage       = "    YuriCloc";  // Shown at startup.
constexpr const char* kDefaultFinalMessage        = "    Good Luc";  // Shown when a countdown ends.
constexpr const char* kDefaultFridaySunsetMessage = "     SUN SET";  // Blinked at Friday sunset.
constexpr const char* kDefaultTradingOpenMessage  = "        OPEN";  // Blinked at a session open.
constexpr const char* kDefaultTradingCloseMessage = "        CLSE";  // Blinked at a session close.

// Default formats are named by key, not by table position. Naming them by
// index is what let this file claim index 7 was " YYYY | MM:DD | hh;mm" when
// the catalog had since grown a row and index 7 had become something else.
// A key either resolves to the format it names or it does not resolve at all.
constexpr const char* kDefaultClockFormat    = "yyyy-mmdd-hhmm";  // Clock mode and Friday's clock phase.
constexpr const char* kDefaultCountingFormat = "ddl-hhmm-ssu";    // Every countdown/count-up view.

// Boundary 2 sits a fifth above Boundary 1 (880 Hz) so the two are distinguishable.
constexpr uint16_t kDefaultBoundary2ToneHz = 1320;

// Resolves a default format key to its index, complaining loudly and falling
// back to the group's first format if the key names nothing. A miss here means
// a key was renamed without updating this file; tools/check_formats.py fails
// the build on exactly that, so this path should be unreachable in a built
// firmware and exists only so a mistake degrades instead of corrupting.
uint8_t formatIndexOrFirst(FormatGroup group, const char* key) {
  uint8_t index = 0;
  if (!displayFormatIndexForKey(group, key, &index)) {
    LOG_PRINTF("default format key \"%s\" is not in the catalog; using the first", key);
    return 0;
  }
  return index;
}

void fillDefaults(ClockConfig& config) {
    // Every field not assigned here already has a default member initializer in
    // config.h, so a field added later is initialized by construction rather
    // than by remembering to add a line to this function.
    config = ClockConfig{};
    // Clock, not Countdown: a countdown default has to name an absolute instant,
    // and any instant that ships in firmware is in the past by the time someone
    // powers the device on - which rendered messages.final on a brand-new clock
    // and read as broken hardware. Clock has no such default to go stale, so the
    // out-of-box state is self-evidently working. kDefaultCountdownEnd is
    // now only a placeholder for a user who selects Countdown without setting an
    // end time, which /format asks for in the same visit.
    config.activeMode = kModeClock;
    config.countdown.format    = formatIndexOrFirst(kFmtGroupCountdown, kDefaultCountingFormat);
    config.countup.format      = formatIndexOrFirst(kFmtGroupCountUp, kDefaultCountingFormat);
    config.display.clockFormat = formatIndexOrFirst(kFmtGroupClock, kDefaultClockFormat);
    config.friday.clockFormat            = config.display.clockFormat;
    config.friday.toFridaySunsetFormat   = config.countdown.format;
    config.friday.toSaturdaySunsetFormat = config.countdown.format;
    config.trading.format                = config.countdown.format;
    config.trading.schedule              = defaultTradingSchedule();
    snprintf(config.countdown.end, sizeof(config.countdown.end), "%s", kDefaultCountdownEnd);
    snprintf(config.countup.start, sizeof(config.countup.start), "%s", kDefaultCountupStart);
    snprintf(config.messages.splash, sizeof(config.messages.splash), "%s", kDefaultSplashMessage);
    snprintf(config.messages.countdownDone, sizeof(config.messages.countdownDone),
             "%s", kDefaultFinalMessage);
    snprintf(config.messages.fridaySunset, sizeof(config.messages.fridaySunset),
             "%s", kDefaultFridaySunsetMessage);
    snprintf(config.messages.tradingOpen, sizeof(config.messages.tradingOpen),
             "%s", kDefaultTradingOpenMessage);
    snprintf(config.messages.tradingClose, sizeof(config.messages.tradingClose),
             "%s", kDefaultTradingCloseMessage);
    // Boundary 2 is the only sound setting that differs from the struct's own
    // defaults (40% volume, event beeps off, 880 Hz / 40 s / 2 Hz patterns).
    config.sound.boundaryAlert.boundary2.toneHz = kDefaultBoundary2ToneHz;
}

}  // namespace

void initDefaultClockConfig(ClockConfig& out) {
    fillDefaults(out);
}

Mode defaultActiveMode() { return kModeClock; }
const char* defaultCountdownEnd() { return kDefaultCountdownEnd; }
const char* defaultCountupStart() { return kDefaultCountupStart; }
const char* defaultClockFormatKey() { return kDefaultClockFormat; }
const char* defaultCountingFormatKey() { return kDefaultCountingFormat; }

TradingSchedule defaultTradingSchedule() {
    TradingSchedule schedule;
    schedule.intervalCount = 1;
    schedule.intervals[0] = {9 * 60 + 30, 16 * 60};
    schedule.intervals[1] = {17 * 60, 18 * 60};
    return schedule;
}

WifiConfig defaultWifiConfig() {
    return WifiConfig{"", "", kDefaultApSsid, kDefaultApPassword};
}
