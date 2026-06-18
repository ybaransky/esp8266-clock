#include "zipcode.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "log.h"

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
  while (*length > 0 && (row[*length - 1] == '\r' || row[*length - 1] == '\n')) {
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

  if (length == rowSize - 1 && file.available()) {
    while (file.available() && file.read() != '\n') {
      yield();
    }
  }

  return length > 0 || file.available();
}

bool rowMatchesZipcode(const char* row, const char* zipcode) {
  return strncmp(row, zipcode, kZipcodeLength) == 0 && row[kZipcodeLength] == ',';
}

bool parseLocationRow(const char* row, ZipcodeLocation* location) {
  if (row == nullptr || location == nullptr || strlen(row) < kZipcodeLength + 4) {
    return false;
  }

  const char* latitudeStart = strchr(row, ',');
  if (latitudeStart == nullptr) {
    return false;
  }
  ++latitudeStart;

  char* latitudeEnd = nullptr;
  const float latitude = strtof(latitudeStart, &latitudeEnd);
  if (latitudeEnd == latitudeStart || *latitudeEnd != ',') {
    return false;
  }

  const char* longitudeStart = latitudeEnd + 1;
  char* longitudeEnd = nullptr;
  const float longitude = strtof(longitudeStart, &longitudeEnd);
  if (longitudeEnd == longitudeStart || *longitudeEnd != '\0') {
    return false;
  }

  memcpy(location->zipcode, row, kZipcodeLength);
  location->zipcode[kZipcodeLength] = '\0';
  location->latitude = latitude;
  location->longitude = longitude;
  return true;
}

bool findZipcodeRow(const char* zipcode, char* row, size_t rowSize, const char* path) {
  if (!isValidZipcode(zipcode) || row == nullptr || rowSize == 0 || path == nullptr) {
    return false;
  }

  if (!STORAGE.begin()) {
    LOG_PRINTLN("STORAGE mount failed - cannot read zipcode file");
    return false;
  }

  File file = STORAGE.open(path, "r");
  if (!file) {
    LOG_PRINTF("Zipcode file not found: %s\n", path);
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
  return false;
}

}  // namespace

bool zipcodeLookupLocation(const char* zipcode, ZipcodeLocation* location, const char* path) {
  if (location == nullptr) {
    return false;
  }

  char row[kMaxZipcodeRowLength];
  if (!findZipcodeRow(zipcode, row, sizeof(row), path)) {
    return false;
  }

  return parseLocationRow(row, location);
}
