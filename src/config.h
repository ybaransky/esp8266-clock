#pragma once
#include <Arduino.h>

#define STORAGE LittleFS

struct ApConfig {
    String ssid;
    String password;
};

ApConfig loadApConfig();
