#include "storage_manager.h"

#include "log.h"

// -----------------------------------------------------------------------------
// StorageManager
// -----------------------------------------------------------------------------

bool StorageManager::ensureMounted(const char* context) {
  if (LittleFS.begin()) {
    return true;
  }

  if ((context == nullptr) || (context[0] == '\0')) {
    LOG_PRINTLN("LittleFS mount failed");
  } else {
    LOG_PRINTF("LittleFS mount failed: %s\n", context);
  }
  return false;
}

StorageManager storageManager;
