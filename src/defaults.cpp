#include "defaults.h"

#include "display_format.h"
#include "log.h"

namespace {

// Empty is a sentinel, not a missing value: WifiConnectionManager derives
// ESP_XXXXXX from the soft-AP MAC whenever the configured SSID is blank, so a
// freshly uploaded filesystem advertises the SDK-style name until the user
// picks one. Keep this in sync with data/config.json.
constexpr const char* kDefaultApSsid     = "";
constexpr const char* kDefaultApPassword = "12345678";

// A placeholder, not a meaningful default: it is already in the past and cannot
// be otherwise, since any date compiled into firmware is stale by the time a
// device is powered on. It is reachable only when a user selects Countdown
// without setting an end time, because the shipped activeMode is Clock. Do not
// duplicate it in data/config.json - patch semantics make an absent field fall
// through to here, and the two copies had already drifted twelve weeks apart.
constexpr const char* kDefaultCountdownDatetime = "2026-07-04 00:00:00";
constexpr const char* kDefaultCountupDatetime   = kCountupStartNow;
constexpr const char* kDefaultSplashMessage       = "    YuriCloc";
constexpr const char* kDefaultFinalMessage        = "    Good Luc";
constexpr const char* kDefaultFridaySunsetMessage = "     SUN SET";
constexpr const char* kDefaultTradingOpenMessage  = "        OPEN";
constexpr const char* kDefaultTradingCloseMessage = "        CLSE";

// Default formats are named by key, not by table position. Naming them by
// index is what let this file claim index 7 was " YYYY | MM:DD | hh;mm" when
// the catalog had since grown a row and index 7 had become something else.
// A key either resolves to the format it names or it does not resolve at all.
constexpr const char* kDefaultClockFormat    = "yyyy-mmdd-hhmm";
constexpr const char* kDefaultCountingFormat = "ddl-hhmm-ssu";

constexpr uint8_t kDefaultSoundVolumePercent = 40;

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

void fillDefaults(ClockConfig& s) {
    // Every field not assigned here already has a default member initializer in
    // config.h, so a field added later is initialized by construction rather
    // than by remembering to add a line to this function.
    s = ClockConfig{};
    // Clock, not Countdown: a countdown default has to name an absolute instant,
    // and any instant that ships in firmware is in the past by the time someone
    // powers the device on - which rendered messages.final on a brand-new clock
    // and read as broken hardware. Clock has no such default to go stale, so the
    // out-of-box state is self-evidently working. kDefaultCountdownDatetime is
    // now only a placeholder for a user who selects Countdown without setting an
    // end time, which /format asks for in the same visit.
    s.activeMode    = kModeClock;
    s.countdown.format = formatIndexOrFirst(kFmtGroupCountdown, kDefaultCountingFormat);
    s.countup.format   = formatIndexOrFirst(kFmtGroupCountUp, kDefaultCountingFormat);
    s.display.clockFmt = formatIndexOrFirst(kFmtGroupClock, kDefaultClockFormat);
    s.friday.clockFmt            = s.display.clockFmt;
    s.friday.toFridaySunsetFmt   = s.countdown.format;
    s.friday.toSaturdaySunsetFmt = s.countdown.format;
    s.trading.format             = s.countdown.format;
    s.trading.schedule = defaultTradingSchedule();
    snprintf(s.countdown.end, sizeof(s.countdown.end), "%s", kDefaultCountdownDatetime);
    snprintf(s.countup.start, sizeof(s.countup.start), "%s", kDefaultCountupDatetime);
    snprintf(s.messages.splash, sizeof(s.messages.splash), "%s", kDefaultSplashMessage);
    snprintf(s.messages.final, sizeof(s.messages.final), "%s", kDefaultFinalMessage);
    snprintf(s.messages.fridaySunset, sizeof(s.messages.fridaySunset),
             "%s", kDefaultFridaySunsetMessage);
    snprintf(s.messages.tradingOpen, sizeof(s.messages.tradingOpen),
             "%s", kDefaultTradingOpenMessage);
    snprintf(s.messages.tradingClose, sizeof(s.messages.tradingClose),
             "%s", kDefaultTradingCloseMessage);
    // Short event beeps default off; accelerating approach alerts stay enabled.
    s.sound.volumePercent = kDefaultSoundVolumePercent;
    // Boundary 2 is the only pattern that differs from the struct's own
    // defaults (880 Hz / 40 s / 2 Hz); it sits a fifth above Boundary 1.
    s.sound.boundaryAlert.boundary2.toneHz = 1320;
}

}  // namespace

void initDefaultClockConfig(ClockConfig& out) {
    fillDefaults(out);
}

Mode defaultActiveMode() { return kModeClock; }
const char* defaultCountdownEnd() { return kDefaultCountdownDatetime; }
const char* defaultCountupStart() { return kDefaultCountupDatetime; }
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
