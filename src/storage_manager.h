#pragma once

#include <Arduino.h>
#include <LittleFS.h>

class StorageManager {
 public:
  bool ensureMounted(const char* context = nullptr);
};

extern StorageManager storageManager;
