#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>
#include "log.h"

static constexpr const char* CONFIG_PATH = "/config.json";
static constexpr const char* YURICLOC = "YuriCloc";
static constexpr const char* PASSWORD = "12345678";

// Opens and deserializes config.json into doc.  Returns false on any failure.
static bool openAndParse(JsonDocument& doc) {
    if (!STORAGE.begin()) {
        LOG_PRINTLN("STORAGE mount failed");
        return false;
    }
    File f = STORAGE.open(CONFIG_PATH, "r");
    if (!f) return false;  // file doesn't exist yet — not an error

    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        LOG_PRINTF("parse error: %s\n", err.c_str());
        return false;
    }
    return true;
}

// ── ClockConfig defaults ──────────────────────────────────────────────────────
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
    return s;
}

// ── WiFi ──────────────────────────────────────────────────────────────────────
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
    if (!STORAGE.begin()) {
        LOG_PRINTLN("STORAGE mount failed - cannot save WiFi config");
        return;
    }
    // Read existing doc so clock settings are preserved.
    JsonDocument doc;
    File fr = STORAGE.open(CONFIG_PATH, "r");
    if (fr) { deserializeJson(doc, fr); fr.close(); }

    doc["staSsid"]         = cfg.staSsid;
    doc["staPassword"]     = cfg.staPassword;
    doc["apSsid"]          = cfg.apSsid;
    doc["apPassword"]      = cfg.apPassword;

    File fw = STORAGE.open(CONFIG_PATH, "w");
    if (!fw) { LOG_PRINTLN("Failed to open config.json for writing"); return; }
    serializeJson(doc, fw);
    fw.close();
    LOG_PRINTLN("WiFi config saved");
}

// ── Clock config ──────────────────────────────────────────────────────────────
ClockConfig ConfigManager::loadClockConfig() {
    ClockConfig s = defaultClockConfig();

    JsonDocument doc;
    if (!openAndParse(doc)) return s;

    const int rawMode = doc["mode"] | (int)kPersistentCountdown;
    s.activeMode    = static_cast<PersistentMode>(rawMode <= (int)kPersistentClock ? rawMode : (int)kPersistentCountdown);
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
    snprintf(s.splashMessage, sizeof(s.splashMessage), "%s", splash);

                                                /*123412341234*/
    const char* finalMsg = doc["finalMessage"] | "Good Luc    ";
    snprintf(s.finalMessage, sizeof(s.finalMessage), "%s", finalMsg);

    return s;
}

void ConfigManager::saveClockConfig(const ClockConfig& s) {
    if (!STORAGE.begin()) {
        LOG_PRINTLN("STORAGE mount failed - cannot save");
        return;
    }
    // Read existing doc so WiFi credentials are preserved.
    JsonDocument doc;
    File fr = STORAGE.open(CONFIG_PATH, "r");
    if (fr) { deserializeJson(doc, fr); fr.close(); }

    doc["mode"]              = static_cast<int>(s.activeMode);
    doc["countdownFmt"]      = s.countdownFmt;
    doc["countupFmt"]        = s.countupFmt;
    doc["clockFmt"]          = s.clockFmt;
    doc["brightness"]        = s.brightness;
    doc["countdownDatetime"] = s.countdownDatetime;
    doc["countupDatetime"]   = s.countupDatetime;
    doc["splashMessage"]     = s.splashMessage;
    doc["finalMessage"]      = s.finalMessage;
    File fw = STORAGE.open(CONFIG_PATH, "w");
    if (!fw) { LOG_PRINTLN("Failed to open config.json for writing"); return; }
    serializeJson(doc, fw);
    fw.close();
    LOG_PRINTLN("Clock config saved");
}

ConfigManager configManager;
