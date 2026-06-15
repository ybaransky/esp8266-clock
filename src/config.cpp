#include "config.h"
#include <LittleFS.h>
#include <ArduinoJson.h>

ApConfig ConfigManager::loadApConfig() {
    ApConfig cfg{"YuriClock", "12345678"};

    if (!STORAGE.begin()) {
        Serial.println("STORAGE mount failed, using default AP config");
        return cfg;
    }

    File f = STORAGE.open("/config.json", "r");
    if (!f) {
        Serial.println("config.json not found, using default AP config");
        return cfg;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, f);
    f.close();

    if (err) {
        Serial.printf("config.json parse error: %s, using defaults\n", err.c_str());
        return cfg;
    }

    cfg.ssid     = doc["ssid"]     | "YuriClock";
    cfg.password = doc["password"] | "12345678";
    return cfg;
}

ConfigManager configManager;
