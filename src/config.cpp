#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config_serializer.h"
#include "config_validation.h"
#include "log.h"
#include "storage_manager.h"

static constexpr const char* CONFIG_PATH = "/config.json";
static constexpr const char* CONFIG_TMP_PATH = "/config.tmp";
static constexpr const char* YURICLOC = "YuriCloc";
static constexpr const char* PASSWORD = "12345678";

static bool writeConfigDocument(JsonDocument& doc, const char* label);
static bool openAndParse(JsonDocument& doc);
static void populateDefaultConfigDocument(JsonDocument& doc);

// Opens and deserializes config.json into doc.  Returns false on any failure.
static bool openAndParse(JsonDocument& doc) {
    if (!storageManager.ensureMounted("open config")) return false;
    File f = STORAGE.open(CONFIG_PATH, "r");
    if (!f) {
        LOG_PRINTLN("config.json not found - creating defaults");
        populateDefaultConfigDocument(doc);
        if (!writeConfigDocument(doc, "create default config")) {
            LOG_PRINTLN("Default config.json create failed - using in-memory defaults");
            return false;
        }
        LOG_PRINTLN("Default config.json created");
        return true;
    }

    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        LOG_PRINTF("config.json parse error: %s\n", err.c_str());
        return false;
    }
    return true;
}

static bool writeConfigDocument(JsonDocument& doc, const char* label) {
    if (!storageManager.ensureMounted(label)) return false;

    STORAGE.remove(CONFIG_TMP_PATH);
    File fw = STORAGE.open(CONFIG_TMP_PATH, "w");
    if (!fw) {
        LOG_PRINTLN("Failed to open config.tmp for writing");
        return false;
    }

    const size_t bytes = serializeJson(doc, fw);
    fw.flush();
    fw.close();
    if (bytes == 0) {
        STORAGE.remove(CONFIG_TMP_PATH);
        LOG_PRINTLN("Failed to serialize config document");
        return false;
    }

    if (!STORAGE.rename(CONFIG_TMP_PATH, CONFIG_PATH)) {
        STORAGE.remove(CONFIG_PATH);
        if (!STORAGE.rename(CONFIG_TMP_PATH, CONFIG_PATH)) {
            STORAGE.remove(CONFIG_TMP_PATH);
            LOG_PRINTLN("Failed to replace config.json");
            return false;
        }
    }
    return true;
}

static void populateDefaultConfigDocument(JsonDocument& doc) {
    const ClockConfig clock = defaultClockConfig();
    const WifiConfig wifi{"", "", YURICLOC, PASSWORD};
    doc.clear();
    serializeClockConfig(doc, clock);
    serializeWifiConfig(doc, wifi);
}

// -- ClockConfig defaults ------------------------------------------------------
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
    snprintf(s.countdownDatetime, sizeof(s.countdownDatetime), "2026-07-04 00:00:00");
    snprintf(s.countupDatetime,   sizeof(s.countupDatetime),   "now");
    snprintf(s.splashMessage, sizeof(s.splashMessage), "YuriCloc");
    snprintf(s.finalMessage,  sizeof(s.finalMessage),  "Good Luc");
    s.location   = {};
    s.sunsetTest = {};
    s.timezone[0] = '\0';
    s.utcOffsetMinutes = 0;
    s.dst = false;
    s.clockUse12Hour = false;
    return s;
}

// -- WiFi ----------------------------------------------------------------------
WifiConfig ConfigManager::loadWifiConfig() {
    WifiConfig cfg{"", "", YURICLOC, PASSWORD};

    JsonDocument doc;
    if (!openAndParse(doc)) return cfg;

    cfg.staSsid     = doc["wifi"]["station"]["ssid"]         | "";
    cfg.staPassword = doc["wifi"]["station"]["password"]     | "";
    cfg.apSsid      = doc["wifi"]["accessPoint"]["ssid"]     | YURICLOC;
    cfg.apPassword  = doc["wifi"]["accessPoint"]["password"] | PASSWORD;
    return cfg;
}

void ConfigManager::saveWifiConfig(const WifiConfig& cfg) {
    if (!storageManager.ensureMounted("save WiFi config")) return;

    JsonDocument doc;
    openAndParse(doc);          // loads existing doc; clock section stays intact
    serializeWifiConfig(doc, cfg);
    if (writeConfigDocument(doc, "save WiFi config")) {
        LOG_PRINTLN("WiFi config saved");
    }
}

// -- Clock config --------------------------------------------------------------
ClockConfig ConfigManager::loadClockConfig() {
    ClockConfig cfg = defaultClockConfig();

    JsonDocument doc;
    if (!openAndParse(doc)) return cfg;

    PersistentMode mode;
    const String activeMode = doc["display"]["activeMode"] | persistentModeName(cfg.activeMode);
    if (persistentModeFromName(activeMode, &mode)) {
        cfg.activeMode = mode;
    }

    cfg.countdownFmt  = doc["display"]["modes"]["countdown"]["format"] | cfg.countdownFmt;
    cfg.countupFmt    = doc["display"]["modes"]["countup"]["format"]   | cfg.countupFmt;
    cfg.clockFmt      = doc["display"]["modes"]["clock"]["format"]     | cfg.clockFmt;
    cfg.fridayClockFmt          = doc["display"]["modes"]["friday"]["clockFormat"]            | cfg.fridayClockFmt;
    cfg.fridayToFridaySunsetFmt = doc["display"]["modes"]["friday"]["toFridaySunsetFormat"]  | cfg.fridayToFridaySunsetFmt;
    cfg.fridayToSatSunsetFmt    = doc["display"]["modes"]["friday"]["toSaturdaySunsetFormat"] | cfg.fridayToSatSunsetFmt;
    cfg.brightness    = doc["display"]["brightness"]                   | cfg.brightness;

    const char* end = doc["display"]["modes"]["countdown"]["end"] | "";
    if (end[0]) snprintf(cfg.countdownDatetime, sizeof(cfg.countdownDatetime), "%s", end);

    const char* start = doc["display"]["modes"]["countup"]["start"] | "";
    if (start[0]) snprintf(cfg.countupDatetime, sizeof(cfg.countupDatetime), "%s", start);

                                               /*123412341234*/
    const char* splash = doc["display"]["messages"]["splash"] | "      hi    ";
    sanitizeDisplayMessage(splash, cfg.splashMessage, sizeof(cfg.splashMessage));

                                                /*123412341234*/
    const char* finalMsg = doc["display"]["messages"]["final"] | "Good Luc    ";
    sanitizeDisplayMessage(finalMsg, cfg.finalMessage, sizeof(cfg.finalMessage));

    JsonVariantConst location = doc["location"];
    cfg.location.latitude  = location["latitude"]  | cfg.location.latitude;
    cfg.location.longitude = location["longitude"] | cfg.location.longitude;
    const char* zipcode = location["zipcode"] | "";
    if (zipcode[0]) {
        snprintf(cfg.location.zipcode, sizeof(cfg.location.zipcode), "%s", zipcode);
    }

    JsonVariantConst sunset = doc["sunset"];
    cfg.sunsetTest.latitude  = sunset["latitude"]  | cfg.sunsetTest.latitude;
    cfg.sunsetTest.longitude = sunset["longitude"] | cfg.sunsetTest.longitude;
    const char* sunsetZipcode = sunset["zipcode"] | "";
    if (sunsetZipcode[0]) {
        snprintf(cfg.sunsetTest.zipcode, sizeof(cfg.sunsetTest.zipcode), "%s", sunsetZipcode);
    }

    const char* timezone = doc["time"]["timezone"]["name"] | "";
    char cleanTimezone[sizeof(cfg.timezone)];
    sanitizePrintableText(timezone, cleanTimezone, sizeof(cleanTimezone));
    if (cleanTimezone[0]) snprintf(cfg.timezone, sizeof(cfg.timezone), "%s", cleanTimezone);
    cfg.utcOffsetMinutes = doc["time"]["timezone"]["utcOffsetMinutes"] | cfg.utcOffsetMinutes;
    cfg.dst = doc["time"]["dst"] | cfg.dst;
    cfg.clockUse12Hour = doc["display"]["clock12Hour"] | cfg.clockUse12Hour;

    return sanitizeClockConfig(cfg);
}

void ConfigManager::saveClockConfig(const ClockConfig& s) {
    if (!storageManager.ensureMounted("save clock config")) return;

    JsonDocument doc;
    openAndParse(doc);          // loads existing doc; wifi section stays intact
    serializeClockConfig(doc, sanitizeClockConfig(s));
    if (writeConfigDocument(doc, "save clock config")) {
        LOG_PRINTLN("Clock config saved");
    }
}

ClockConfig ConfigManager::sanitizeClockConfig(const ClockConfig& cfg) const {
    const ClockConfig defaults = defaultClockConfig();
    ClockConfig clean = cfg;
    clean.activeMode   = sanitizePersistentMode(static_cast<int>(cfg.activeMode), defaults.activeMode);
    clean.countdownFmt = sanitizeFormatIndex(kFmtGroupCountdown, cfg.countdownFmt, defaults.countdownFmt);
    clean.countupFmt   = sanitizeFormatIndex(kFmtGroupCountUp,   cfg.countupFmt,   defaults.countupFmt);
    clean.clockFmt     = sanitizeFormatIndex(kFmtGroupClock,     cfg.clockFmt,     defaults.clockFmt);
    clean.fridayClockFmt          = sanitizeFormatIndex(kFmtGroupClock,     cfg.fridayClockFmt,          defaults.fridayClockFmt);
    clean.fridayToFridaySunsetFmt = sanitizeFormatIndex(kFmtGroupCountdown, cfg.fridayToFridaySunsetFmt, defaults.fridayToFridaySunsetFmt);
    clean.fridayToSatSunsetFmt    = sanitizeFormatIndex(kFmtGroupCountdown, cfg.fridayToSatSunsetFmt,    defaults.fridayToSatSunsetFmt);
    clean.brightness   = sanitizeBrightness(cfg.brightness);
    clean.utcOffsetMinutes = sanitizeUtcOffsetMinutes(cfg.utcOffsetMinutes);
    sanitizeDisplayMessage(cfg.splashMessage, clean.splashMessage, sizeof(clean.splashMessage));
    sanitizeDisplayMessage(cfg.finalMessage,  clean.finalMessage,  sizeof(clean.finalMessage));
    sanitizePrintableText(cfg.timezone, clean.timezone, sizeof(clean.timezone));
    return clean;
}

ConfigManager configManager;
