#include "config_serializer.h"

#include <ArduinoJson.h>

#include "config.h"
#include "config_validation.h"
#include "zipcode.h"

namespace {

String formatTimeOfDay(uint16_t minute) {
  const uint8_t hour = minute / 60U;
  const uint8_t min = minute % 60U;
  char value[6] = {static_cast<char>('0' + hour / 10U),
                   static_cast<char>('0' + hour % 10U), ':',
                   static_cast<char>('0' + min / 10U),
                   static_cast<char>('0' + min % 10U), '\0'};
  return String(value);
}

bool parseTimeOfDay(const char* value, uint16_t* minute) {
  if ((value == nullptr) || (minute == nullptr) ||
      (strlen(value) != 5) || (value[2] != ':') ||
      (value[0] < '0') || (value[0] > '9') ||
      (value[1] < '0') || (value[1] > '9') ||
      (value[3] < '0') || (value[3] > '9') ||
      (value[4] < '0') || (value[4] > '9')) {
    return false;
  }
  const uint8_t hour = (value[0] - '0') * 10 + (value[1] - '0');
  const uint8_t min = (value[3] - '0') * 10 + (value[4] - '0');
  if ((hour > 23) || (min > 59)) return false;
  *minute = hour * 60U + min;
  return true;
}

}  // namespace

void serializeClockConfig(JsonDocument& doc, const ClockConfig& clock) {
  JsonObject display = doc["display"].to<JsonObject>();
  display["activeMode"]  = modeName(clock.activeMode);
  display["brightness"]  = clock.display.brightness;
  display["clock12Hour"] = clock.display.clockUse12Hour;

  JsonObject messages = display["messages"].to<JsonObject>();
  messages["splash"]       = String(clock.messages.splash);
  messages["final"]        = String(clock.messages.final);
  messages["fridaySunset"] = String(clock.messages.fridaySunset);
  messages["tradingOpen"]  = String(clock.messages.tradingOpen);
  messages["tradingClose"] = String(clock.messages.tradingClose);

  JsonObject modes = display["modes"].to<JsonObject>();

  JsonObject countdown = modes["countdown"].to<JsonObject>();
  countdown["format"] = clock.countdown.format;
  countdown["end"]    = String(clock.countdown.end);

  JsonObject countup = modes["countup"].to<JsonObject>();
  countup["format"] = clock.countup.format;
  countup["start"]  = String(clock.countup.start);

  JsonObject clockMode = modes["clock"].to<JsonObject>();
  clockMode["format"] = clock.display.clockFmt;

  JsonObject friday = modes["friday"].to<JsonObject>();
  friday["clockFormat"]            = clock.friday.clockFmt;
  friday["toFridaySunsetFormat"]   = clock.friday.toFridaySunsetFmt;
  friday["toSaturdaySunsetFormat"] = clock.friday.toSaturdaySunsetFmt;

  JsonObject trading = modes["trading"].to<JsonObject>();
  trading["format"] = clock.trading.format;
  trading["formatOver24"] = clock.trading.formatOver24;
  trading["intervalCount"] = clock.trading.schedule.intervalCount;
  JsonArray intervals = trading["intervals"].to<JsonArray>();
  for (uint8_t i = 0; i < kMaxTradingIntervals; ++i) {
    JsonObject interval = intervals.add<JsonObject>();
    interval["start"] =
        formatTimeOfDay(clock.trading.schedule.intervals[i].startMinute);
    interval["stop"] =
        formatTimeOfDay(clock.trading.schedule.intervals[i].stopMinute);
  }

  JsonObject timezone = doc["time"]["timezone"].to<JsonObject>();
  timezone["name"] = String(clock.timezone.name);
  timezone["utcOffsetMinutes"] = clock.timezone.utcOffsetMinutes;

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
  if ((zipcode == nullptr) || ((zipcode[0] != '\0') && !isValidZipcode(zipcode))) {
    return false;
  }
  snprintf(destination, destinationSize, "%s", zipcode);
  return true;
}

// One row per format-index field: where it lives in the JSON (modes[modeKey]
// [fieldKey]), which format group governs valid indexes, whether kSameFormat
// is an accepted value, and how to reach the target ClockConfig member. Using
// an accessor instead of a member pointer lets one table cover fields nested
// at different depths (e.g. cfg.countdown.format vs cfg.display.clockFmt).
struct FormatFieldDescriptor {
  const char* modeKey;
  const char* fieldKey;
  FormatGroup group;
  bool optional;  // True if kSameFormat/-1 means "no secondary format".
  uint8_t& (*field)(ClockConfig&);
};

const FormatFieldDescriptor kFormatFields[] = {
    {"countdown", "format", kFmtGroupCountdown, false,
     [](ClockConfig& c) -> uint8_t& { return c.countdown.format; }},
    {"countup", "format", kFmtGroupCountUp, false,
     [](ClockConfig& c) -> uint8_t& { return c.countup.format; }},
    {"clock", "format", kFmtGroupClock, false,
     [](ClockConfig& c) -> uint8_t& { return c.display.clockFmt; }},
    {"friday", "clockFormat", kFmtGroupClock, false,
     [](ClockConfig& c) -> uint8_t& { return c.friday.clockFmt; }},
    {"friday", "toFridaySunsetFormat", kFmtGroupCountdown, false,
     [](ClockConfig& c) -> uint8_t& { return c.friday.toFridaySunsetFmt; }},
    {"friday", "toSaturdaySunsetFormat", kFmtGroupCountdown, false,
     [](ClockConfig& c) -> uint8_t& { return c.friday.toSaturdaySunsetFmt; }},
    {"trading", "format", kFmtGroupCountdown, false,
     [](ClockConfig& c) -> uint8_t& { return c.trading.format; }},
    {"trading", "formatOver24", kFmtGroupCountdown, true,
     [](ClockConfig& c) -> uint8_t& { return c.trading.formatOver24; }},
};

// One row per free-text display-message field: JSON key under "messages" and
// how to reach the target fixed-size buffer.
struct MessageFieldDescriptor {
  const char* jsonKey;
  char* (*field)(ClockConfig&);
  size_t size;
};

const MessageFieldDescriptor kMessageFields[] = {
    {"splash", [](ClockConfig& c) -> char* { return c.messages.splash; },
     sizeof(MessageConfig::splash)},
    {"final", [](ClockConfig& c) -> char* { return c.messages.final; },
     sizeof(MessageConfig::final)},
    {"fridaySunset",
     [](ClockConfig& c) -> char* { return c.messages.fridaySunset; },
     sizeof(MessageConfig::fridaySunset)},
    {"tradingOpen",
     [](ClockConfig& c) -> char* { return c.messages.tradingOpen; },
     sizeof(MessageConfig::tradingOpen)},
    {"tradingClose",
     [](ClockConfig& c) -> char* { return c.messages.tradingClose; },
     sizeof(MessageConfig::tradingClose)},
};

void applyFormatFields(JsonVariantConst display, JsonVariantConst modes, ClockConfig& cfg) {
  for (const FormatFieldDescriptor& d : kFormatFields) {
    JsonVariantConst value = modes[d.modeKey][d.fieldKey];
    if (value.isNull()) continue;
    uint8_t& field = d.field(cfg);
    field = d.optional
        ? sanitizeOptionalFormatIndex(d.group, value.as<int>(), field)
        : sanitizeFormatIndex(d.group, value.as<int>(), field);
  }
  if (!display["brightness"].isNull()) {
    cfg.display.brightness = sanitizeBrightness(display["brightness"].as<int>());
  }
  if (!display["clock12Hour"].isNull()) {
    cfg.display.clockUse12Hour = display["clock12Hour"].as<bool>();
  }
  if (!modes["countdown"]["end"].isNull()) {
    snprintf(cfg.countdown.end, sizeof(cfg.countdown.end), "%s",
             modes["countdown"]["end"].as<const char*>());
  }
  if (!modes["countup"]["start"].isNull()) {
    snprintf(cfg.countup.start, sizeof(cfg.countup.start), "%s",
             modes["countup"]["start"].as<const char*>());
  }
}

bool applyTradingSchedule(JsonVariantConst trading, ClockConfig& cfg) {
  const bool hasIntervals = !trading["intervals"].isNull();
  const bool hasCount = !trading["intervalCount"].isNull();
  if (!hasIntervals && !hasCount) return true;
  TradingSchedule candidate = cfg.trading.schedule;
  if (hasIntervals) {
    JsonArrayConst intervals = trading["intervals"].as<JsonArrayConst>();
    if ((intervals.size() < 1) || (intervals.size() > kMaxTradingIntervals)) {
      return false;
    }
    if (!hasCount) candidate.intervalCount = intervals.size();  // Legacy JSON.
    for (uint8_t i = 0; i < intervals.size(); ++i) {
      JsonObjectConst interval = intervals[i].as<JsonObjectConst>();
      const char* start = interval["start"].as<const char*>();
      const char* stop = interval["stop"].as<const char*>();
      if (!parseTimeOfDay(start, &candidate.intervals[i].startMinute) ||
          !parseTimeOfDay(stop, &candidate.intervals[i].stopMinute)) {
        return false;
      }
    }
  }
  if (hasCount) {
    const int intervalCount = trading["intervalCount"].as<int>();
    if ((intervalCount < 1) || (intervalCount > kMaxTradingIntervals)) {
      return false;
    }
    candidate.intervalCount = intervalCount;
  }
  if (!isValidTradingSchedule(candidate)) return false;
  cfg.trading.schedule = candidate;
  return true;
}

void applyMessageFields(JsonVariantConst messages, ClockConfig& cfg) {
  for (const MessageFieldDescriptor& d : kMessageFields) {
    JsonVariantConst value = messages[d.jsonKey];
    if (value.isNull()) continue;
    sanitizeDisplayMessage(value.as<const char*>(), d.field(cfg), d.size);
  }
}

bool applyLocationInfo(JsonVariantConst source, LocationInfo& info) {
  if (!source["latitude"].isNull()) {
    info.latitude = source["latitude"].as<float>();
  }
  if (!source["longitude"].isNull()) {
    info.longitude = source["longitude"].as<float>();
  }
  sanitizeLocationInfo(info);
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
                          cfg.timezone.name,
                          sizeof(cfg.timezone.name));
  }
  if (!timezone["utcOffsetMinutes"].isNull()) {
    cfg.timezone.utcOffsetMinutes =
        sanitizeUtcOffsetMinutes(timezone["utcOffsetMinutes"].as<int>());
  }
}

}  // namespace

void sanitizeFormatFields(ClockConfig& cfg, const ClockConfig& defaults) {
  ClockConfig& mutableDefaults = const_cast<ClockConfig&>(defaults);
  for (const FormatFieldDescriptor& d : kFormatFields) {
    uint8_t& field = d.field(cfg);
    const uint8_t fallback = d.field(mutableDefaults);
    field = d.optional
        ? sanitizeOptionalFormatIndex(d.group, field, fallback)
        : sanitizeFormatIndex(d.group, field, fallback);
  }
}

void sanitizeMessageFields(ClockConfig& cfg) {
  for (const MessageFieldDescriptor& d : kMessageFields) {
    char* field = d.field(cfg);
    sanitizeDisplayMessage(field, field, d.size);
  }
}

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
  if (!applyTradingSchedule(display["modes"]["trading"], cfg) &&
      (error == nullptr)) {
    error = "{\"error\":\"Trading sessions must be valid, ordered, and separated\"}";
  }
  applyMessageFields(display["messages"], cfg);

  const bool locationOk =
      applyLocationInfo(root["location"], cfg.locations.device);
  const bool sunsetOk =
      applyLocationInfo(root["sunset"], cfg.locations.sunsetTest);
  if ((!locationOk || !sunsetOk) && (error == nullptr)) {
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
