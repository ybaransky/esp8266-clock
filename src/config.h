#pragma once
#include <Arduino.h>

#define STORAGE LittleFS

struct ApConfig {
    String ssid;
    String password;
};

class ConfigManager {
public:
    ApConfig loadApConfig();
};

extern ConfigManager configManager;
