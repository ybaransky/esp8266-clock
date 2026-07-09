#include "defaults.h"

namespace {

constexpr const char* kDefaultApSsid     = "YuriCloc";
constexpr const char* kDefaultApPassword = "12345678";

constexpr const char* kDefaultCountdownDatetime = "2026-07-04 00:00:00";
constexpr const char* kDefaultCountupDatetime   = "now";
constexpr const char* kDefaultSplashMessage     = "    YuriCloc";
constexpr const char* kDefaultFinalMessage      = "    Good Luc";

}  // namespace

ClockConfig defaultClockConfig() {
    ClockConfig s;
    s.activeMode    = kPersistentCountdown;
    s.countdownFmt  = 0; // "dd D | hh:mm |  ss.u"
    s.countupFmt    = 0;
    s.clockFmt      = 7; // " YYYY | MM:DD | hh;mm" (blinking colon)
    s.fridayClockFmt          = 7;
    s.fridayToFridaySunsetFmt = 0;
    s.fridayToSatSunsetFmt    = 0;
    s.brightness    = 3;
    snprintf(s.countdownDatetime, sizeof(s.countdownDatetime), "%s", kDefaultCountdownDatetime);
    snprintf(s.countupDatetime,   sizeof(s.countupDatetime),   "%s", kDefaultCountupDatetime);
    snprintf(s.splashMessage, sizeof(s.splashMessage), "%s", kDefaultSplashMessage);
    snprintf(s.finalMessage,  sizeof(s.finalMessage),  "%s", kDefaultFinalMessage);
    s.location   = {};
    s.sunsetTest = {};
    s.timezone[0] = '\0';
    s.utcOffsetMinutes = 0;
    s.dst = false;
    s.clockUse12Hour = false;
    return s;
}

WifiConfig defaultWifiConfig() {
    return WifiConfig{"", "", kDefaultApSsid, kDefaultApPassword};
}
