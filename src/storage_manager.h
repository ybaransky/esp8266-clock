#pragma once

#include <Arduino.h>
#include <LittleFS.h>

// The filesystem every module opens files through. Named here, next to the
// manager that mounts it, so no caller has to pull in unrelated headers to
// reach the filesystem.
#define STORAGE LittleFS

// Mounts LittleFS on demand and reports context-rich failures to the log.
class StorageManager {
 public:
  bool ensureMounted(const char* context = nullptr);
};

extern StorageManager storageManager;
