#include "config_serializer.h"

#include <ArduinoJson.h>

#include "config.h"
#include "config_validation.h"
#include "datetime_validation.h"
#include "defaults.h"
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
  uint8_t (*get)(const ClockConfig&);  // Read-only twin, for serialization.
};

const FormatFieldDescriptor kFormatFields[] = {
    {"countdown", "format", kFmtGroupCountdown, false,
     [](ClockConfig& c) -> uint8_t& { return c.countdown.format; },
     [](const ClockConfig& c) { return c.countdown.format; }},
    {"countup", "format", kFmtGroupCountUp, false,
     [](ClockConfig& c) -> uint8_t& { return c.countup.format; },
     [](const ClockConfig& c) { return c.countup.format; }},
    {"clock", "format", kFmtGroupClock, false,
     [](ClockConfig& c) -> uint8_t& { return c.display.clockFmt; },
     [](const ClockConfig& c) { return c.display.clockFmt; }},
    {"friday", "clockFormat", kFmtGroupClock, false,
     [](ClockConfig& c) -> uint8_t& { return c.friday.clockFmt; },
     [](const ClockConfig& c) { return c.friday.clockFmt; }},
    {"friday", "toFridaySunsetFormat", kFmtGroupCountdown, false,
     [](ClockConfig& c) -> uint8_t& { return c.friday.toFridaySunsetFmt; },
     [](const ClockConfig& c) { return c.friday.toFridaySunsetFmt; }},
    {"friday", "toSaturdaySunsetFormat", kFmtGroupCountdown, false,
     [](ClockConfig& c) -> uint8_t& { return c.friday.toSaturdaySunsetFmt; },
     [](const ClockConfig& c) { return c.friday.toSaturdaySunsetFmt; }},
    {"trading", "format", kFmtGroupCountdown, false,
     [](ClockConfig& c) -> uint8_t& { return c.trading.format; },
     [](const ClockConfig& c) { return c.trading.format; }},
    {"trading", "formatOver24", kFmtGroupCountdown, true,
     [](ClockConfig& c) -> uint8_t& { return c.trading.formatOver24; },
     [](const ClockConfig& c) { return c.trading.formatOver24; }},
};

// The value written for an optional format field that is not in use, and the
// value the web dropdowns post back for "same as normal format".
constexpr char kSameFormatKey[] = "same";

// One row per free-text display-message field: JSON key under "messages" and
// how to reach the target fixed-size buffer.
struct MessageFieldDescriptor {
  const char* jsonKey;
  char* (*field)(ClockConfig&);
  const char* (*get)(const ClockConfig&);  // Read-only twin, for serialization.
  size_t size;
};

const MessageFieldDescriptor kMessageFields[] = {
    {"splash", [](ClockConfig& c) -> char* { return c.messages.splash; },
     [](const ClockConfig& c) -> const char* { return c.messages.splash; },
     sizeof(MessageConfig::splash)},
    {"final", [](ClockConfig& c) -> char* { return c.messages.final; },
     [](const ClockConfig& c) -> const char* { return c.messages.final; },
     sizeof(MessageConfig::final)},
    {"fridaySunset",
     [](ClockConfig& c) -> char* { return c.messages.fridaySunset; },
     [](const ClockConfig& c) -> const char* { return c.messages.fridaySunset; },
     sizeof(MessageConfig::fridaySunset)},
    {"tradingOpen",
     [](ClockConfig& c) -> char* { return c.messages.tradingOpen; },
     [](const ClockConfig& c) -> const char* { return c.messages.tradingOpen; },
     sizeof(MessageConfig::tradingOpen)},
    {"tradingClose",
     [](ClockConfig& c) -> char* { return c.messages.tradingClose; },
     [](const ClockConfig& c) -> const char* { return c.messages.tradingClose; },
     sizeof(MessageConfig::tradingClose)},
};

// One row per per-event sound selection: JSON key under "sound" and how to
// reach the target fixed-size buffer. Deliberately parallel to kMessageFields -
// every announced event has both a message and a sound, and the two tables are
// what keep that pairing from drifting.
struct SoundFieldDescriptor {
  const char* jsonKey;
  char* (*field)(ClockConfig&);
  const char* (*get)(const ClockConfig&);  // Read-only twin, for serialization.
};

const SoundFieldDescriptor kSoundFields[] = {
    {"startup", [](ClockConfig& c) -> char* { return c.sound.startup; },
     [](const ClockConfig& c) -> const char* { return c.sound.startup; }},
    {"final", [](ClockConfig& c) -> char* { return c.sound.final; },
     [](const ClockConfig& c) -> const char* { return c.sound.final; }},
    {"fridaySunset",
     [](ClockConfig& c) -> char* { return c.sound.fridaySunset; },
     [](const ClockConfig& c) -> const char* { return c.sound.fridaySunset; }},
    {"tradingOpen",
     [](ClockConfig& c) -> char* { return c.sound.tradingOpen; },
     [](const ClockConfig& c) -> const char* { return c.sound.tradingOpen; }},
    {"tradingClose",
     [](ClockConfig& c) -> char* { return c.sound.tradingClose; },
     [](const ClockConfig& c) -> const char* { return c.sound.tradingClose; }},
};

}  // namespace

// Every string written here is a `const char*` pointing into `clock` or into a
// static table, never a String copy: ArduinoJson stores such a pointer by
// reference rather than duplicating the text into its pool, which is the
// difference between a handful of heap allocations per save and none.
//
// LIFETIME REQUIREMENT: `clock` must outlive `doc`. Both callers satisfy this
// (ConfigManager serializes a reference to its own cached config; the HTTP
// handler reads the manager's cache directly). Do not pass a temporary.
void serializeClockConfig(JsonDocument& doc, const ClockConfig& clock) {
  // Schema version, so a future incompatible change has something to branch on
  // instead of guessing from which keys happen to be present.
  doc["configVersion"] = kConfigSchemaVersion;

  JsonObject display = doc["display"].to<JsonObject>();
  display["activeMode"]  = modeName(clock.activeMode);
  display["brightness"]  = clock.display.brightness;
  display["clock12Hour"] = clock.display.clockUse12Hour;

  JsonObject messages = display["messages"].to<JsonObject>();
  for (const MessageFieldDescriptor& d : kMessageFields) {
    messages[d.jsonKey] = d.get(clock);
  }

  JsonObject sound = doc["sound"].to<JsonObject>();
  sound["enabled"] = clock.sound.enabled;
  sound["volume"]  = clock.sound.volumePercent;
  for (const SoundFieldDescriptor& d : kSoundFields) {
    sound[d.jsonKey] = d.get(clock);
  }
  JsonObject boundaryAlert = sound["boundaryAlert"].to<JsonObject>();
  boundaryAlert["enabled"] = clock.sound.boundaryAlert.enabled;
  JsonObject boundary1 = boundaryAlert["boundary1"].to<JsonObject>();
  boundary1["toneHz"] = clock.sound.boundaryAlert.boundary1.toneHz;
  boundary1["totalDurationSeconds"] =
      clock.sound.boundaryAlert.boundary1.totalDurationSeconds;
  boundary1["startingBeatsHz"] =
      clock.sound.boundaryAlert.boundary1.startingBeatsHz;
  JsonObject boundary2 = boundaryAlert["boundary2"].to<JsonObject>();
  boundary2["toneHz"] = clock.sound.boundaryAlert.boundary2.toneHz;
  boundary2["totalDurationSeconds"] =
      clock.sound.boundaryAlert.boundary2.totalDurationSeconds;
  boundary2["startingBeatsHz"] =
      clock.sound.boundaryAlert.boundary2.startingBeatsHz;

  JsonObject modes = display["modes"].to<JsonObject>();

  // Format selections are written as stable keys, never as table positions, so
  // adding or reordering a format cannot silently repoint a saved config at a
  // different one. Read back by applyFormatFields(), which still accepts a
  // legacy integer index from configs written before this change.
  // Nested subscript assignment creates the intermediate mode objects on
  // demand; .to<JsonObject>() would clear a mode that an earlier row already
  // populated (both "friday" and "trading" appear more than once here).
  for (const FormatFieldDescriptor& d : kFormatFields) {
    const uint8_t index = d.get(clock);
    if (d.optional && (index == kSameFormat)) {
      modes[d.modeKey][d.fieldKey] = kSameFormatKey;
      continue;
    }
    // The key table is in flash, so it is copied out here. ArduinoJson copies
    // this local into its own pool, unlike the const char* fields above.
    char key[kFormatKeyLength];
    displayFormatKey(d.group, index, key, sizeof(key));
    modes[d.modeKey][d.fieldKey] = key;
  }

  modes["countdown"]["end"] = clock.countdown.end;
  modes["countup"]["start"] = clock.countup.start;
  modes["friday"]["blinkBeforeMinutes"] = clock.friday.blinkBeforeMinutes;
  modes["friday"]["blinkAfterMinutes"]  = clock.friday.blinkAfterMinutes;
  modes["trading"]["intervalCount"] = clock.trading.schedule.intervalCount;
  JsonArray intervals = modes["trading"]["intervals"].to<JsonArray>();
  for (uint8_t i = 0; i < kMaxTradingIntervals; ++i) {
    JsonObject interval = intervals.add<JsonObject>();
    // These two are genuine temporaries, so ArduinoJson must copy them - the
    // only String values written by this function, and deliberately so.
    interval["start"] =
        formatTimeOfDay(clock.trading.schedule.intervals[i].startMinute);
    interval["stop"] =
        formatTimeOfDay(clock.trading.schedule.intervals[i].stopMinute);
  }

  JsonObject timezone = doc["time"]["timezone"].to<JsonObject>();
  timezone["name"] = clock.timezone.name;
  timezone["utcOffsetMinutes"] = clock.timezone.utcOffsetMinutes;

  JsonObject location = doc["location"].to<JsonObject>();
  location["zipcode"]   = clock.locations.device.zipcode;
  location["latitude"]  = clock.locations.device.latitude;
  location["longitude"] = clock.locations.device.longitude;

  JsonObject sunset = doc["sunset"].to<JsonObject>();
  sunset["zipcode"]   = clock.locations.sunsetTest.zipcode;
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

// Sound names are stored as given, not checked against the catalog: the
// catalog lives on the filesystem and can be re-uploaded independently of
// config.json, so a name that matches nothing today may match tomorrow.
// SoundPlayer treats a miss as silence and logs it once.
void applySoundFields(JsonVariantConst sound, ClockConfig& cfg) {
  if (!sound["enabled"].isNull()) {
    cfg.sound.enabled = sound["enabled"].as<bool>();
  }
  if (!sound["volume"].isNull()) {
    cfg.sound.volumePercent = sanitizeVolumePercent(sound["volume"].as<int>());
  }
  for (const SoundFieldDescriptor& d : kSoundFields) {
    JsonVariantConst value = sound[d.jsonKey];
    if (value.isNull()) continue;
    sanitizePrintableText(value.as<const char*>(), d.field(cfg),
                          kSoundNameLength);
  }
  JsonVariantConst boundaryAlert = sound["boundaryAlert"];
  if (!boundaryAlert["enabled"].isNull()) {
    cfg.sound.boundaryAlert.enabled = boundaryAlert["enabled"].as<bool>();
  }
  SoundConfig::BoundaryPatternConfig* patterns[] = {
      &cfg.sound.boundaryAlert.boundary1,
      &cfg.sound.boundaryAlert.boundary2};
  const char* keys[] = {"boundary1", "boundary2"};
  for (uint8_t i = 0; i < 2; ++i) {
    JsonVariantConst source = boundaryAlert[keys[i]];
    if (!source["toneHz"].isNull()) {
      patterns[i]->toneHz =
          sanitizeBoundaryFrequencyHz(source["toneHz"].as<int>());
    }
    if (!source["totalDurationSeconds"].isNull()) {
      patterns[i]->totalDurationSeconds = sanitizeBoundaryDurationSeconds(
          source["totalDurationSeconds"].as<int>());
    }
    if (!source["startingBeatsHz"].isNull()) {
      patterns[i]->startingBeatsHz = sanitizeBoundaryStartingBeatsHz(
          source["startingBeatsHz"].as<int>());
    }
  }

}

// Resolves one stored format selection onto its config field.
//
// A string is a stable format key, which is what this firmware writes. An
// integer is a legacy table index from a config written before keys existed;
// it is still honoured so an existing /config.json loads unchanged, and the
// next save rewrites it as a key. Anything unrecognized leaves the field at
// whatever the caller started from (defaults on load, the previous value on
// patch), which is the same fallback behavior the index sanitizers had.
void applyFormatField(const FormatFieldDescriptor& d, JsonVariantConst value,
                      ClockConfig& cfg) {
  uint8_t& field = d.field(cfg);

  if (value.is<const char*>()) {
    const char* key = value.as<const char*>();
    if (d.optional && (key != nullptr) && (strcmp(key, kSameFormatKey) == 0)) {
      field = kSameFormat;
      return;
    }
    uint8_t resolved = field;
    if (displayFormatIndexForKey(d.group, key, &resolved)) field = resolved;
    return;
  }

  if (value.is<int>()) {
    field = d.optional
                ? sanitizeOptionalFormatIndex(d.group, value.as<int>(), field)
                : sanitizeFormatIndex(d.group, value.as<int>(), field);
  }
}

void applyFormatFields(JsonVariantConst display, JsonVariantConst modes, ClockConfig& cfg) {
  for (const FormatFieldDescriptor& d : kFormatFields) {
    JsonVariantConst value = modes[d.modeKey][d.fieldKey];
    if (value.isNull()) continue;
    applyFormatField(d, value, cfg);
  }
  if (!display["brightness"].isNull()) {
    cfg.display.brightness = sanitizeBrightness(display["brightness"].as<int>());
  }
  if (!display["clock12Hour"].isNull()) {
    cfg.display.clockUse12Hour = display["clock12Hour"].as<bool>();
  }
  if (!modes["friday"]["blinkBeforeMinutes"].isNull()) {
    cfg.friday.blinkBeforeMinutes =
        sanitizeBlinkMinutes(modes["friday"]["blinkBeforeMinutes"].as<int>());
  }
  if (!modes["friday"]["blinkAfterMinutes"].isNull()) {
    cfg.friday.blinkAfterMinutes =
        sanitizeBlinkMinutes(modes["friday"]["blinkAfterMinutes"].as<int>());
  }
}

bool applyDateTimeField(JsonVariantConst value, char* destination,
                        size_t size, bool allowNow) {
  if (value.isNull()) return true;
  const char* text = value.as<const char*>();
  if (text == nullptr) return false;
  if (allowNow && (strcmp(text, "now") == 0)) {
    strlcpy(destination, "now", size);
    return true;
  }
  DateTime parsed;
  if (!parseLocalDateTime(text, parsed)) return false;
  // Validation guarantees fixed-width digits; normalize browser minutes and T.
  char canonical[20];
  strlcpy(canonical, text, sizeof(canonical));
  canonical[10] = ' ';
  if (strlen(text) == 16) strlcpy(canonical + 16, ":00", 4);
  strlcpy(destination, canonical, size);
  return true;
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

void sanitizeFormatFields(ClockConfig& cfg) {
  // The fallback for a field is its group's default format, resolved from the
  // default key. That is both cheaper than materializing a whole default
  // ClockConfig and more honest: the fallback for a countdown format field is
  // "the default counting format", not "whatever field the defaults happen to
  // hold in the same slot".
  uint8_t countingFallback = 0;
  uint8_t clockFallback = 0;
  displayFormatIndexForKey(kFmtGroupCountdown, defaultCountingFormatKey(),
                           &countingFallback);
  displayFormatIndexForKey(kFmtGroupClock, defaultClockFormatKey(),
                           &clockFallback);

  for (const FormatFieldDescriptor& d : kFormatFields) {
    uint8_t& field = d.field(cfg);
    const uint8_t fallback =
        (d.group == kFmtGroupClock) ? clockFallback : countingFallback;
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

void sanitizeSoundFields(ClockConfig& cfg) {
  cfg.sound.volumePercent = sanitizeVolumePercent(cfg.sound.volumePercent);
  SoundConfig::BoundaryPatternConfig* patterns[] = {
      &cfg.sound.boundaryAlert.boundary1,
      &cfg.sound.boundaryAlert.boundary2};
  for (SoundConfig::BoundaryPatternConfig* pattern : patterns) {
    pattern->toneHz = sanitizeBoundaryFrequencyHz(pattern->toneHz);
    pattern->totalDurationSeconds = sanitizeBoundaryDurationSeconds(
        pattern->totalDurationSeconds);
    pattern->startingBeatsHz =
        sanitizeBoundaryStartingBeatsHz(pattern->startingBeatsHz);
  }
  for (const SoundFieldDescriptor& d : kSoundFields) {
    char* field = d.field(cfg);
    sanitizePrintableText(field, field, kSoundNameLength);
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
  const JsonVariantConst modes = display["modes"];
  const bool countdownOk = applyDateTimeField(modes["countdown"]["end"],
      cfg.countdown.end, sizeof(cfg.countdown.end), false);
  const bool countupOk = applyDateTimeField(modes["countup"]["start"],
      cfg.countup.start, sizeof(cfg.countup.start), true);
  if ((!countdownOk || !countupOk) && (error == nullptr)) {
    error = "{\"error\":\"Invalid countdown or count-up datetime\"}";
  }
  if (!applyTradingSchedule(display["modes"]["trading"], cfg) &&
      (error == nullptr)) {
    error = "{\"error\":\"Trading sessions must be valid, ordered, and separated\"}";
  }
  applyMessageFields(display["messages"], cfg);
  applySoundFields(root["sound"], cfg);

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
