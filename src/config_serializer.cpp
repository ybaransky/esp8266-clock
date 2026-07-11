#include "config_serializer.h"

#include <ArduinoJson.h>

#include "config.h"
#include "config_validation.h"
#include "zipcode.h"

void serializeClockConfig(JsonDocument& doc, const ClockConfig& clock) {
  JsonObject display = doc["display"].to<JsonObject>();
  display["activeMode"]  = modeName(clock.activeMode);
  display["brightness"]  = clock.display.brightness;
  display["clock12Hour"] = clock.display.clockUse12Hour;

  JsonObject messages = display["messages"].to<JsonObject>();
  messages["splash"]       = String(clock.messages.splash);
  messages["final"]        = String(clock.messages.final);
  messages["fridaySunset"] = String(clock.messages.fridaySunset);

  JsonObject modes = display["modes"].to<JsonObject>();

  JsonObject countdown = modes["countdown"].to<JsonObject>();
  countdown["format"] = clock.display.countdownFmt;
  countdown["end"]    = String(clock.countdownDatetime);

  JsonObject countup = modes["countup"].to<JsonObject>();
  countup["format"] = clock.display.countupFmt;
  countup["start"]  = String(clock.countupDatetime);

  JsonObject clockMode = modes["clock"].to<JsonObject>();
  clockMode["format"] = clock.display.clockFmt;

  JsonObject friday = modes["friday"].to<JsonObject>();
  friday["clockFormat"]            = clock.friday.clockFmt;
  friday["toFridaySunsetFormat"]   = clock.friday.toFridaySunsetFmt;
  friday["toSaturdaySunsetFormat"] = clock.friday.toSaturdaySunsetFmt;

  JsonObject timezone = doc["time"]["timezone"].to<JsonObject>();
  timezone["name"]           = String(clock.timezone);
  timezone["utcOffsetMinutes"] = clock.utcOffsetMinutes;
  doc["time"]["dst"]         = clock.dst;

  JsonObject location = doc["location"].to<JsonObject>();
  location["zipcode"]   = String(clock.locations.device.zipcode);
  location["latitude"]  = clock.locations.device.latitude;
  location["longitude"] = clock.locations.device.longitude;

  JsonObject sunset = doc["sunset"].to<JsonObject>();
  sunset["zipcode"]   = String(clock.locations.sunsetTest.zipcode);
  sunset["latitude"]  = clock.locations.sunsetTest.latitude;
  sunset["longitude"] = clock.locations.sunsetTest.longitude;
}

void serializeWifiConfig(JsonDocument& doc, const WifiConfig& wifi) {
  JsonObject wifiDoc = doc["wifi"].to<JsonObject>();

  JsonObject station = wifiDoc["station"].to<JsonObject>();
  station["ssid"]     = wifi.staSsid;
  station["password"] = wifi.staPassword;

  JsonObject accessPoint = wifiDoc["accessPoint"].to<JsonObject>();
  accessPoint["ssid"]     = wifi.apSsid;
  accessPoint["password"] = wifi.apPassword;
}

void serializeWifiStatus(JsonDocument& doc, const WifiConfig& wifi) {
  JsonObject wifiDoc = doc["wifi"].to<JsonObject>();

  JsonObject station = wifiDoc["station"].to<JsonObject>();
  station["ssid"] = wifi.staSsid;
  // station password intentionally omitted from HTTP responses

  JsonObject accessPoint = wifiDoc["accessPoint"].to<JsonObject>();
  accessPoint["ssid"]     = wifi.apSsid;
  accessPoint["password"] = wifi.apPassword;
}

// -- JSON -> struct (patch semantics) --------------------------------------------

namespace {

bool applyZipcode(const char* zipcode, char* destination, size_t destinationSize) {
  if (zipcode == nullptr || (zipcode[0] != '\0' && !isValidZipcode(zipcode))) {
    return false;
  }
  snprintf(destination, destinationSize, "%s", zipcode);
  return true;
}

void applyFormatFields(JsonVariantConst display, JsonVariantConst modes, ClockConfig& cfg) {
  if (!modes["countdown"]["format"].isNull()) {
    cfg.display.countdownFmt = sanitizeFormatIndex(
        kFmtGroupCountdown, modes["countdown"]["format"].as<int>(), cfg.display.countdownFmt);
  }
  if (!modes["countup"]["format"].isNull()) {
    cfg.display.countupFmt = sanitizeFormatIndex(
        kFmtGroupCountUp, modes["countup"]["format"].as<int>(), cfg.display.countupFmt);
  }
  if (!modes["clock"]["format"].isNull()) {
    cfg.display.clockFmt = sanitizeFormatIndex(
        kFmtGroupClock, modes["clock"]["format"].as<int>(), cfg.display.clockFmt);
  }
  if (!modes["friday"]["clockFormat"].isNull()) {
    cfg.friday.clockFmt = sanitizeFormatIndex(
        kFmtGroupClock, modes["friday"]["clockFormat"].as<int>(), cfg.friday.clockFmt);
  }
  if (!modes["friday"]["toFridaySunsetFormat"].isNull()) {
    cfg.friday.toFridaySunsetFmt = sanitizeFormatIndex(
        kFmtGroupCountdown,
        modes["friday"]["toFridaySunsetFormat"].as<int>(),
        cfg.friday.toFridaySunsetFmt);
  }
  if (!modes["friday"]["toSaturdaySunsetFormat"].isNull()) {
    cfg.friday.toSaturdaySunsetFmt = sanitizeFormatIndex(
        kFmtGroupCountdown,
        modes["friday"]["toSaturdaySunsetFormat"].as<int>(),
        cfg.friday.toSaturdaySunsetFmt);
  }
  if (!display["brightness"].isNull()) {
    cfg.display.brightness = sanitizeBrightness(display["brightness"].as<int>());
  }
  if (!display["clock12Hour"].isNull()) {
    cfg.display.clockUse12Hour = display["clock12Hour"].as<bool>();
  }
  if (!modes["countdown"]["end"].isNull()) {
    snprintf(cfg.countdownDatetime, sizeof(cfg.countdownDatetime), "%s",
             modes["countdown"]["end"].as<const char*>());
  }
  if (!modes["countup"]["start"].isNull()) {
    snprintf(cfg.countupDatetime, sizeof(cfg.countupDatetime), "%s",
             modes["countup"]["start"].as<const char*>());
  }
}

void applyMessageFields(JsonVariantConst messages, ClockConfig& cfg) {
  if (!messages["splash"].isNull()) {
    sanitizeDisplayMessage(messages["splash"].as<const char*>(),
                           cfg.messages.splash,
                           sizeof(cfg.messages.splash));
  }
  if (!messages["final"].isNull()) {
    sanitizeDisplayMessage(messages["final"].as<const char*>(),
                           cfg.messages.final,
                           sizeof(cfg.messages.final));
  }
  if (!messages["fridaySunset"].isNull()) {
    sanitizeDisplayMessage(messages["fridaySunset"].as<const char*>(),
                           cfg.messages.fridaySunset,
                           sizeof(cfg.messages.fridaySunset));
  }
}

bool applyLocationInfo(JsonVariantConst source, LocationInfo& info) {
  if (!source["latitude"].isNull()) {
    info.latitude = source["latitude"].as<float>();
  }
  if (!source["longitude"].isNull()) {
    info.longitude = source["longitude"].as<float>();
  }
  if (!source["zipcode"].isNull()) {
    if (!applyZipcode(source["zipcode"].as<const char*>(),
                      info.zipcode,
                      sizeof(info.zipcode))) {
      return false;
    }
  }
  return true;
}

void applyTimezoneFields(JsonVariantConst time, ClockConfig& cfg) {
  JsonVariantConst timezone = time["timezone"];
  if (!timezone["name"].isNull()) {
    sanitizePrintableText(timezone["name"].as<const char*>(),
                          cfg.timezone,
                          sizeof(cfg.timezone));
  }
  if (!timezone["utcOffsetMinutes"].isNull()) {
    cfg.utcOffsetMinutes = sanitizeUtcOffsetMinutes(timezone["utcOffsetMinutes"].as<int>());
  }
  if (!time["dst"].isNull()) {
    cfg.dst = time["dst"].as<bool>();
  }
}

}  // namespace

const char* applyJsonToClockConfig(JsonVariantConst root, ClockConfig& cfg) {
  // Every present field is applied even after an invalid one is seen, so a
  // single bad value in config.json can't wipe out the rest of the file on
  // load. The first error is still reported for API callers, which discard
  // the partially updated cfg.
  const char* error = nullptr;
  JsonVariantConst display = root["display"];

  if (!display["activeMode"].isNull()) {
    Mode nextMode;
    if (modeFromName(display["activeMode"] | "", &nextMode)) {
      cfg.activeMode = nextMode;
    } else {
      error = "{\"error\":\"Invalid active mode\"}";
    }
  }

  applyFormatFields(display, display["modes"], cfg);
  applyMessageFields(display["messages"], cfg);

  const bool locationOk =
      applyLocationInfo(root["location"], cfg.locations.device);
  const bool sunsetOk =
      applyLocationInfo(root["sunset"], cfg.locations.sunsetTest);
  if ((!locationOk || !sunsetOk) && error == nullptr) {
    error = "{\"error\":\"ZIP code must be 5 digits\"}";
  }

  applyTimezoneFields(root["time"], cfg);
  return error;
}

bool applyJsonToWifiConfig(JsonVariantConst root, WifiConfig& wifi) {
  JsonVariantConst station = root["wifi"]["station"];
  JsonVariantConst accessPoint = root["wifi"]["accessPoint"];

  bool changed = false;
  if (!station["ssid"].isNull()) {
    wifi.staSsid = station["ssid"].as<String>();
    changed = true;
  }
  if (!station["password"].isNull()) {
    wifi.staPassword = station["password"].as<String>();
    changed = true;
  }
  if (!accessPoint["ssid"].isNull()) {
    wifi.apSsid = accessPoint["ssid"].as<String>();
    changed = true;
  }
  if (!accessPoint["password"].isNull()) {
    wifi.apPassword = accessPoint["password"].as<String>();
    changed = true;
  }
  return changed;
}
