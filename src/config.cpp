#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config_validation.h"
#include "log.h"
#include "storage_manager.h"

static constexpr const char* CONFIG_PATH = "/config.json";
static constexpr const char* CONFIG_TMP_PATH = "/config.tmp";
static constexpr const char* YURICLOC = "YuriCloc";
static constexpr const char* PASSWORD = "12345678";

static bool writeConfigDocument(JsonDocument& doc, const char* label);
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

// -- ClockConfig defaults ------------------------------------------------------
ClockConfig defaultClockConfig() {
    ClockConfig s;
    s.activeMode    = kPersistentCountdown;
    s.countdownFmt  = 0; // "dd D | hh:mm |  ss.u"
    s.countupFmt    = 0;
    s.clockFmt      = 1; // " YYYY | MM:DD | hh:mm" (static colon)
    s.brightness    = 3;
    snprintf(s.countdownDatetime, sizeof(s.countdownDatetime), "2026-07-04 00:00:00");
    snprintf(s.countupDatetime,   sizeof(s.countupDatetime),   "now");
    snprintf(s.splashMessage, sizeof(s.splashMessage), "YuriCloc");
    snprintf(s.finalMessage,  sizeof(s.finalMessage),  "Good Luc");
    s.latitude = 0.0f;
    s.longitude = 0.0f;
    s.zipcode[0] = '\0';
    s.timezone[0] = '\0';
    s.utcOffsetMinutes = 0;
    return s;
}

static void populateDefaultConfigDocument(JsonDocument& doc) {
    const ClockConfig clock = defaultClockConfig();
    doc.clear();
    doc["mode"]              = static_cast<int>(clock.activeMode);
    doc["countdownFmt"]      = clock.countdownFmt;
    doc["countupFmt"]        = clock.countupFmt;
    doc["clockFmt"]          = clock.clockFmt;
    doc["brightness"]        = clock.brightness;
    doc["countdownDatetime"] = String(clock.countdownDatetime);
    doc["countupDatetime"]   = String(clock.countupDatetime);
    doc["splashMessage"]     = String(clock.splashMessage);
    doc["finalMessage"]      = String(clock.finalMessage);
    doc["latitude"]          = clock.latitude;
    doc["longitude"]         = clock.longitude;
    doc["zipcode"]           = String(clock.zipcode);
    doc["timezone"]          = String(clock.timezone);
    doc["utcOffsetMinutes"]  = clock.utcOffsetMinutes;
    doc["staSsid"]           = "";
    doc["staPassword"]       = "";
    doc["apSsid"]            = YURICLOC;
    doc["apPassword"]        = PASSWORD;
}

// -- WiFi ----------------------------------------------------------------------
WifiConfig ConfigManager::loadWifiConfig() {
    WifiConfig cfg{"", "", YURICLOC, PASSWORD};

    JsonDocument doc;
    if (!openAndParse(doc)) return cfg;

    cfg.staSsid         = doc["staSsid"]         | "";
    cfg.staPassword     = doc["staPassword"]     | "";
    cfg.apSsid          = doc["apSsid"]          | YURICLOC;
    cfg.apPassword      = doc["apPassword"]      | PASSWORD;
    return cfg;
}

void ConfigManager::saveWifiConfig(const WifiConfig& cfg) {
    if (!storageManager.ensureMounted("save WiFi config")) return;

    // Read existing doc so clock settings are preserved.
    JsonDocument doc;
    File fr = STORAGE.open(CONFIG_PATH, "r");
    if (fr) { deserializeJson(doc, fr); fr.close(); }

    doc["staSsid"]         = cfg.staSsid;
    doc["staPassword"]     = cfg.staPassword;
    doc["apSsid"]          = cfg.apSsid;
    doc["apPassword"]      = cfg.apPassword;

    if (writeConfigDocument(doc, "save WiFi config")) {
        LOG_PRINTLN("WiFi config saved");
    }
}

// -- Clock config --------------------------------------------------------------
ClockConfig ConfigManager::loadClockConfig() {
    ClockConfig s = defaultClockConfig();

    JsonDocument doc;
    if (!openAndParse(doc)) return s;

    s.activeMode    = static_cast<PersistentMode>(doc["mode"] | static_cast<int>(s.activeMode));
    s.countdownFmt  = doc["countdownFmt"]  | s.countdownFmt;
    s.countupFmt    = doc["countupFmt"]    | s.countupFmt;
    s.clockFmt      = doc["clockFmt"]      | s.clockFmt;
    s.brightness    = doc["brightness"]    | s.brightness;

    const char* target = doc["countdownDatetime"] | "";
    if (target[0]) snprintf(s.countdownDatetime, sizeof(s.countdownDatetime), "%s", target);

    const char* start = doc["countupDatetime"] | "";
    if (start[0])  snprintf(s.countupDatetime,  sizeof(s.countupDatetime),   "%s", start);

                                               /*123412341234*/
    const char* splash = doc["splashMessage"] | "      hi    ";
    sanitizeDisplayMessage(splash, s.splashMessage, sizeof(s.splashMessage));

                                                /*123412341234*/
    const char* finalMsg = doc["finalMessage"] | "Good Luc    ";
    sanitizeDisplayMessage(finalMsg, s.finalMessage, sizeof(s.finalMessage));

    s.latitude = doc["latitude"] | s.latitude;
    s.longitude = doc["longitude"] | s.longitude;
    const char* zipcode = doc["zipcode"] | "";
    if (zipcode[0]) snprintf(s.zipcode, sizeof(s.zipcode), "%s", zipcode);

    const char* timezone = doc["timezone"] | "";
    char cleanTimezone[sizeof(s.timezone)];
    sanitizePrintableText(timezone, cleanTimezone, sizeof(cleanTimezone));
    if (cleanTimezone[0]) snprintf(s.timezone, sizeof(s.timezone), "%s", cleanTimezone);
    s.utcOffsetMinutes = doc["utcOffsetMinutes"] | s.utcOffsetMinutes;

    return sanitizeClockConfig(s);
}

void ConfigManager::saveClockConfig(const ClockConfig& s) {
    if (!storageManager.ensureMounted("save clock config")) return;

    const ClockConfig clean = sanitizeClockConfig(s);

    // Read existing doc so WiFi credentials are preserved.
    JsonDocument doc;
    File fr = STORAGE.open(CONFIG_PATH, "r");
    if (fr) { deserializeJson(doc, fr); fr.close(); }

    doc["mode"]              = static_cast<int>(clean.activeMode);
    doc["countdownFmt"]      = clean.countdownFmt;
    doc["countupFmt"]        = clean.countupFmt;
    doc["clockFmt"]          = clean.clockFmt;
    doc["brightness"]        = clean.brightness;
    doc["countdownDatetime"] = String(clean.countdownDatetime);
    doc["countupDatetime"]   = String(clean.countupDatetime);
    doc["splashMessage"]     = String(clean.splashMessage);
    doc["finalMessage"]      = String(clean.finalMessage);
    doc["latitude"]          = clean.latitude;
    doc["longitude"]         = clean.longitude;
    doc["zipcode"]           = String(clean.zipcode);
    doc["timezone"]          = String(clean.timezone);
    doc["utcOffsetMinutes"]  = clean.utcOffsetMinutes;
    if (writeConfigDocument(doc, "save clock config")) {
        LOG_PRINTLN("Clock config saved");
    }
}

ClockConfig ConfigManager::sanitizeClockConfig(const ClockConfig& cfg) const {
    const ClockConfig defaults = defaultClockConfig();
    ClockConfig clean = cfg;
    clean.activeMode = sanitizePersistentMode(static_cast<int>(cfg.activeMode), defaults.activeMode);
    clean.countdownFmt = sanitizeFormatIndex(kFmtGroupCountdown, cfg.countdownFmt, defaults.countdownFmt);
    clean.countupFmt = sanitizeFormatIndex(kFmtGroupCountUp, cfg.countupFmt, defaults.countupFmt);
    clean.clockFmt = sanitizeFormatIndex(kFmtGroupClock, cfg.clockFmt, defaults.clockFmt);
    clean.brightness = sanitizeBrightness(cfg.brightness);
    clean.utcOffsetMinutes = sanitizeUtcOffsetMinutes(cfg.utcOffsetMinutes);
    sanitizeDisplayMessage(cfg.splashMessage, clean.splashMessage, sizeof(clean.splashMessage));
    sanitizeDisplayMessage(cfg.finalMessage, clean.finalMessage, sizeof(clean.finalMessage));
    sanitizePrintableText(cfg.timezone, clean.timezone, sizeof(clean.timezone));
    return clean;
}

ConfigManager configManager;
