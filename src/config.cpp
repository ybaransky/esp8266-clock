#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

static constexpr const char* CONFIG_PATH = "/config.json";

// Opens and deserializes config.json into doc.  Returns false on any failure.
static bool openAndParse(JsonDocument& doc) {
    if (!STORAGE.begin()) {
        Serial.println("[CFG] STORAGE mount failed");
        return false;
    }
    File f = STORAGE.open(CONFIG_PATH, "r");
    if (!f) return false;  // file doesn't exist yet — not an error

    DeserializationError err = deserializeJson(doc, f);
    f.close();
    if (err) {
        Serial.printf("[CFG] parse error: %s\n", err.c_str());
        return false;
    }
    return true;
}

// ── ClockConfig defaults ──────────────────────────────────────────────────────
ClockConfig defaultClockConfig() {
    ClockConfig s;
    s.activeMode    = kBaseCountdown;
    s.countdownFmt  = 0; // "dd D | hh:mm |  ss.u"
    s.countupFmt    = 0;
    s.clockFmt      = 1; // " YYYY | MM:DD | hh:mm" (static colon)
    s.justification = 1; // Right justified
    s.brightness    = 3;
    snprintf(s.countdownDatetime, sizeof(s.countdownDatetime), "2026-07-04 00:00:00");
    snprintf(s.countupDatetime,   sizeof(s.countupDatetime),   "now");
    snprintf(s.splashMessage, sizeof(s.splashMessage), "YuriCloc");
    snprintf(s.finalMessage,  sizeof(s.finalMessage),  "Good Luc");
    return s;
}

// ── WiFi ──────────────────────────────────────────────────────────────────────
WifiConfig ConfigManager::loadWifiConfig() {
    WifiConfig cfg{"YuriClock", "12345678"};

    JsonDocument doc;
    if (!openAndParse(doc)) return cfg;

    cfg.ssid     = doc["ssid"]     | "YuriClock";
    cfg.password = doc["password"] | "12345678";
    return cfg;
}

void ConfigManager::saveWifiConfig(const WifiConfig& cfg) {
    if (!STORAGE.begin()) {
        Serial.println("[CFG] STORAGE mount failed — cannot save WiFi config");
        return;
    }
    // Read existing doc so clock settings are preserved.
    JsonDocument doc;
    File fr = STORAGE.open(CONFIG_PATH, "r");
    if (fr) { deserializeJson(doc, fr); fr.close(); }

    doc["ssid"]     = cfg.ssid;
    doc["password"] = cfg.password;

    File fw = STORAGE.open(CONFIG_PATH, "w");
    if (!fw) { Serial.println("[CFG] Failed to open config.json for writing"); return; }
    serializeJson(doc, fw);
    fw.close();
    Serial.println("[CFG] WiFi config saved");
}

// ── Clock config ──────────────────────────────────────────────────────────────
ClockConfig ConfigManager::loadClockConfig() {
    ClockConfig s = defaultClockConfig();

    JsonDocument doc;
    if (!openAndParse(doc)) return s;

    const int rawMode = doc["mode"] | (int)kBaseCountdown;
    s.activeMode    = static_cast<BaseMode>(rawMode <= (int)kBaseClock ? rawMode : (int)kBaseCountdown);
    s.countdownFmt  = doc["countdownFmt"]  | s.countdownFmt;
    s.countupFmt    = doc["countupFmt"]    | s.countupFmt;
    s.clockFmt      = doc["clockFmt"]      | s.clockFmt;
    s.justification = doc["justification"] | s.justification;
    s.brightness    = doc["brightness"]    | s.brightness;

    const char* target = doc["countdownDatetime"] | "";
    if (target[0]) snprintf(s.countdownDatetime, sizeof(s.countdownDatetime), "%s", target);

    const char* start = doc["countupDatetime"] | "";
    if (start[0])  snprintf(s.countupDatetime,  sizeof(s.countupDatetime),   "%s", start);

    const char* splash = doc["splashMessage"] | "";
    snprintf(s.splashMessage, sizeof(s.splashMessage), "%s", splash);

    const char* finalMsg = doc["finalMessage"] | "";
    snprintf(s.finalMessage, sizeof(s.finalMessage), "%s", finalMsg);

    return s;
}

void ConfigManager::saveClockConfig(const ClockConfig& s) {
    if (!STORAGE.begin()) {
        Serial.println("[CFG] STORAGE mount failed — cannot save");
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
    doc["justification"]     = s.justification;
    doc["brightness"]        = s.brightness;
    doc["countdownDatetime"] = s.countdownDatetime;
    doc["countupDatetime"]   = s.countupDatetime;
    doc["splashMessage"]     = s.splashMessage;
    doc["finalMessage"]      = s.finalMessage;
    doc.remove("infoMessage");

    File fw = STORAGE.open(CONFIG_PATH, "w");
    if (!fw) { Serial.println("[CFG] Failed to open config.json for writing"); return; }
    serializeJson(doc, fw);
    fw.close();
    Serial.println("[CFG] Clock config saved");
}

// ── Utility ───────────────────────────────────────────────────────────────────
String ConfigManager::loadRaw() {
    if (!STORAGE.begin()) return String();
    File f = STORAGE.open(CONFIG_PATH, "r");
    if (!f) return String();
    String s = f.readString();
    f.close();
    return s;
}

bool ConfigManager::deleteConfig() {
    if (!STORAGE.begin()) return false;
    if (!STORAGE.exists(CONFIG_PATH)) return false;
    return STORAGE.remove(CONFIG_PATH);
}

ConfigManager configManager;
