#include "log.h"

#include "rtc_ds3231.h"

#include <string.h>

namespace {

const char* baseName(const char* path) {
  const char* fileName = path;
  for (const char* cursor = path; *cursor != '\0'; ++cursor) {
    if (*cursor == '/' || *cursor == '\\') {
      fileName = cursor + 1;
    }
  }
  return fileName;
}

const char* extensionStart(const char* fileName) {
  const char* extension = nullptr;
  for (const char* cursor = fileName; *cursor != '\0'; ++cursor) {
    if (*cursor == '.') {
      extension = cursor;
    }
  }
  return extension;
}

}  // namespace

const char* logSourceName(const char* path) {
  const char* fileName = baseName(path);
  const char* extension = extensionStart(fileName);
  const size_t length = extension == nullptr
      ? strlen(fileName)
      : static_cast<size_t>(extension - fileName);

  static char sourceName[32];
  const size_t maxLength = sizeof(sourceName) - 1;
  const size_t copyLength = length < maxLength ? length : maxLength;
  memcpy(sourceName, fileName, copyLength);
  sourceName[copyLength] = '\0';
  return sourceName;
}

const char* logCurrentTime() {
  static char timeText[12];
  const RtcStatus status = rtcGetStatus();
  if (!status.present) {
    snprintf(timeText, sizeof(timeText), "--:--:--");
    return timeText;
  }

  const DateTime now = rtcGetNow();
  snprintf(timeText, sizeof(timeText), "%02d:%02d:%02d",
           now.hour(), now.minute(), now.second());
  return timeText;
}
