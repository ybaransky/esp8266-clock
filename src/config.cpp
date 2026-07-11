#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "config_serializer.h"
#include "config_validation.h"
#include "defaults.h"
#include "log.h"
#include "storage_manager.h"

static constexpr const char* CONFIG_PATH = "/config.json";
static constexpr const char* CONFIG_TMP_PATH = "/config.tmp";

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
    const WifiConfig wifi = defaultWifiConfig();
    doc.clear();
    serializeClockConfig(doc, clock);
    serializeWifiConfig(doc, wifi);
}

// -- WiFi ----------------------------------------------------------------------
WifiConfig ConfigManager::loadWifiConfig() {
    WifiConfig cfg = defaultWifiConfig();

    JsonDocument doc;
    if (!openAndParse(doc)) return cfg;

    applyJsonToWifiConfig(doc.as<JsonVariantConst>(), cfg);
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

    // Invalid values are skipped; the field keeps its default.
    const char* error = applyJsonToClockConfig(doc.as<JsonVariantConst>(), cfg);
    if (error != nullptr) {
        LOG_PRINTF("config.json has invalid values: %s\n", error);
    }
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
    clean.activeMode   = sanitizeMode(static_cast<int>(cfg.activeMode), defaults.activeMode);
    clean.display.countdownFmt = sanitizeFormatIndex(kFmtGroupCountdown, cfg.display.countdownFmt, defaults.display.countdownFmt);
    clean.display.countupFmt   = sanitizeFormatIndex(kFmtGroupCountUp,   cfg.display.countupFmt,   defaults.display.countupFmt);
    clean.display.clockFmt     = sanitizeFormatIndex(kFmtGroupClock,     cfg.display.clockFmt,     defaults.display.clockFmt);
    clean.friday.clockFmt = sanitizeFormatIndex(
        kFmtGroupClock, cfg.friday.clockFmt, defaults.friday.clockFmt);
    clean.friday.toFridaySunsetFmt = sanitizeFormatIndex(
        kFmtGroupCountdown, cfg.friday.toFridaySunsetFmt,
        defaults.friday.toFridaySunsetFmt);
    clean.friday.toSaturdaySunsetFmt = sanitizeFormatIndex(
        kFmtGroupCountdown, cfg.friday.toSaturdaySunsetFmt,
        defaults.friday.toSaturdaySunsetFmt);
    clean.display.brightness = sanitizeBrightness(cfg.display.brightness);
    clean.utcOffsetMinutes = sanitizeUtcOffsetMinutes(cfg.utcOffsetMinutes);
    sanitizeDisplayMessage(cfg.messages.splash, clean.messages.splash, sizeof(clean.messages.splash));
    sanitizeDisplayMessage(cfg.messages.final, clean.messages.final, sizeof(clean.messages.final));
    sanitizeDisplayMessage(cfg.messages.fridaySunset, clean.messages.fridaySunset,
                           sizeof(clean.messages.fridaySunset));
    sanitizePrintableText(cfg.timezone, clean.timezone, sizeof(clean.timezone));
    return clean;
}

ConfigManager configManager;
