#include "defaults.h"

namespace {

constexpr const char* kDefaultApSsid     = "YuriCloc";
constexpr const char* kDefaultApPassword = "12345678";

constexpr const char* kDefaultCountdownDatetime = "2026-07-04 00:00:00";
constexpr const char* kDefaultCountupDatetime   = "now";
constexpr const char* kDefaultSplashMessage       = "    YuriCloc";
constexpr const char* kDefaultFinalMessage        = "    Good Luc";
constexpr const char* kDefaultFridaySunsetMessage = "     SUN SET";

}  // namespace

ClockConfig defaultClockConfig() {
    ClockConfig s;
    s.activeMode    = kModeCountdown;
    s.countdown.format      = 0; // "dd D | hh:mm |  ss.u"
    s.countup.format        = 0;
    s.display.clockFmt      = 7; // " YYYY | MM:DD | hh;mm" (blinking colon)
    s.friday.clockFmt             = 7;
    s.friday.toFridaySunsetFmt    = 0;
    s.friday.toSaturdaySunsetFmt  = 0;
    s.display.brightness = 3;
    snprintf(s.countdown.end, sizeof(s.countdown.end), "%s", kDefaultCountdownDatetime);
    snprintf(s.countup.start, sizeof(s.countup.start), "%s", kDefaultCountupDatetime);
    snprintf(s.messages.splash, sizeof(s.messages.splash), "%s", kDefaultSplashMessage);
    snprintf(s.messages.final, sizeof(s.messages.final), "%s", kDefaultFinalMessage);
    snprintf(s.messages.fridaySunset, sizeof(s.messages.fridaySunset),
             "%s", kDefaultFridaySunsetMessage);
    s.locations = {};
    s.timezone.name[0] = '\0';
    s.timezone.utcOffsetMinutes = 0;
    s.dst = false;
    s.display.clockUse12Hour = false;
    return s;
}

WifiConfig defaultWifiConfig() {
    return WifiConfig{"", "", kDefaultApSsid, kDefaultApPassword};
}
