#include "log.h"

#include <cont.h>
#include <string.h>

namespace {

LogTimeProvider timeProvider = nullptr;  // Cached-time source; nullptr prints --:--:--.

}  // namespace

const char* logSourceName(PGM_P path) {
  // Flash must be read through pgm_read_byte; direct byte loads would fault.
  PGM_P fileName = path;
  for (PGM_P cursor = path;; ++cursor) {
    const char c = static_cast<char>(pgm_read_byte(cursor));
    if (c == '\0') break;
    if ((c == '/') || (c == '\\')) fileName = cursor + 1;
  }

  static char sourceName[32];
  strncpy_P(sourceName, fileName, sizeof(sourceName) - 1);
  sourceName[sizeof(sourceName) - 1] = '\0';
  char* extension = strrchr(sourceName, '.');
  if (extension != nullptr) *extension = '\0';
  return sourceName;
}

const char* logCurrentTime() {
  static char timeText[12];
  if ((timeProvider != nullptr) && timeProvider(timeText, sizeof(timeText))) {
    return timeText;
  }

  snprintf(timeText, sizeof(timeText), "--:--:--");
  return timeText;
}

void logSetTimeProvider(LogTimeProvider provider) {
  timeProvider = provider;
}

unsigned logStackUsed() {
  return static_cast<unsigned>(CONT_STACKSIZE - ESP.getFreeContStack());
}
