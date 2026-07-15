#include "zipcode.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "log.h"
#include "storage_manager.h"

namespace {

constexpr size_t kZipcodeLength = 5;
constexpr size_t kMaxZipcodeRowLength = 64;

}  // namespace

bool isValidZipcode(const char* zipcode) {
  if (zipcode == nullptr) {
    return false;
  }

  for (size_t index = 0; index < kZipcodeLength; ++index) {
    if (!isdigit(static_cast<unsigned char>(zipcode[index]))) {
      return false;
    }
  }
  return zipcode[kZipcodeLength] == '\0';
}

namespace {

void trimLineEnding(char* row, size_t* length) {
  while ((*length > 0) && ((row[*length - 1] == '\r') || (row[*length - 1] == '\n'))) {
    row[--(*length)] = '\0';
  }
}

bool readCsvLine(File& file, char* row, size_t rowSize) {
  if (rowSize == 0) {
    return false;
  }

  const size_t length = file.readBytesUntil('\n', row, rowSize - 1);
  row[length] = '\0';

  size_t trimmedLength = length;
  trimLineEnding(row, &trimmedLength);

  if ((length == rowSize - 1) && file.available()) {
    while (file.available() && (file.read() != '\n')) {
      yield();
    }
  }

  return (length > 0) || file.available();
}

bool rowMatchesZipcode(const char* row, const char* zipcode) {
  return (strncmp(row, zipcode, kZipcodeLength) == 0) && (row[kZipcodeLength] == ',');
}

bool parseLocationRow(const char* row, ZipcodeLocation* location) {
  if ((row == nullptr) || (location == nullptr) || (strlen(row) < kZipcodeLength + 4)) {
    return false;
  }

  const char* latitudeStart = strchr(row, ',');
  if (latitudeStart == nullptr) {
    return false;
  }
  ++latitudeStart;

  char* latitudeEnd = nullptr;
  const float latitude = strtof(latitudeStart, &latitudeEnd);
  if ((latitudeEnd == latitudeStart) || (*latitudeEnd != ',')) {
    return false;
  }

  const char* longitudeStart = latitudeEnd + 1;
  char* longitudeEnd = nullptr;
  const float longitude = strtof(longitudeStart, &longitudeEnd);
  if ((longitudeEnd == longitudeStart) || (*longitudeEnd != '\0')) {
    return false;
  }

  memcpy(location->zipcode, row, kZipcodeLength);
  location->zipcode[kZipcodeLength] = '\0';
  location->latitude = latitude;
  location->longitude = longitude;
  return true;
}

bool findZipcodeRow(const char* zipcode, char* row, size_t rowSize, const char* path) {
  if (!isValidZipcode(zipcode) || (row == nullptr) || (rowSize == 0) || (path == nullptr)) {
    LOG_PRINTF("Zipcode lookup invalid arguments: zip=\"%s\" row=%s rowSize=%u path=\"%s\"",
               zipcode == nullptr ? "(null)" : zipcode,
               row == nullptr ? "null" : "set",
               static_cast<unsigned>(rowSize),
               path == nullptr ? "(null)" : path);
    return false;
  }

  if (!storageManager.ensureMounted("read zipcode file")) {
    return false;
  }

  File file = STORAGE.open(path, "r");
  if (!file) {
    LOG_PRINTF("Zipcode file not found: %s", path);
    return false;
  }

  while (file.available()) {
    if (!readCsvLine(file, row, rowSize)) {
      break;
    }

    if (rowMatchesZipcode(row, zipcode)) {
      file.close();
      return true;
    }

    yield();
  }

  file.close();
  row[0] = '\0';
  LOG_PRINTF("Zipcode not found in %s: %s", path, zipcode);
  return false;
}

}  // namespace

bool zipcodeLookupLocation(const char* zipcode, ZipcodeLocation* location, const char* path) {
  if (location == nullptr) {
    LOG_PRINTF("Zipcode lookup failed: null output for zip=\"%s\"",
               zipcode == nullptr ? "(null)" : zipcode);
    return false;
  }

  char row[kMaxZipcodeRowLength];
  if (!findZipcodeRow(zipcode, row, sizeof(row), path)) {
    return false;
  }

  if (!parseLocationRow(row, location)) {
    LOG_PRINTF("Zipcode lookup failed: malformed row for zip=\"%s\": \"%s\"",
               zipcode == nullptr ? "(null)" : zipcode,
               row);
    return false;
  }

  return true;
}
