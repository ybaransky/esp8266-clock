#pragma once

#include <Arduino.h>
#include <LittleFS.h>

// Mounts LittleFS on demand and reports context-rich failures to the log.
class StorageManager {
 public:
  bool ensureMounted(const char* context = nullptr);
};

extern StorageManager storageManager;
