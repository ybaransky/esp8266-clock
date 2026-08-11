#include "zipcode.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <ctype.h>
#include <string.h>

#include "log.h"
#include "storage_manager.h"

namespace {

// Binary layout of /zipcodes.bin, produced by tools/build_zipcodes.py from
// assets/zipcodes.csv. That script and this file are the only two places the
// layout is spelled out; change them together.
//
//   [0]     8 bytes       "ZIPB", uint8 version, uint8 recordSize,
//                         uint16 recordCount
//   [8]     2002 bytes    directory: 1001 x uint16 cumulative record index, one
//                         per three-digit prefix plus a terminating entry
//   [2010]  5 bytes each  records in ZIP order: uint8 suffix, int16 latitude,
//                         int16 longitude, both in hundredths of a degree
//
// A prefix's records are [directory[prefix], directory[prefix + 1]), so one
// four-byte read yields both where the bucket starts and how long it is. The
// prefix itself is implied by the directory slot and never stored.

constexpr size_t kZipcodeLength = 5;
constexpr size_t kPrefixLength = 3;

constexpr char kMagic[] = "ZIPB";
constexpr size_t kMagicLength = 4;
constexpr uint8_t kFormatVersion = 1;
constexpr size_t kRecordSize = 5;
constexpr size_t kHeaderSize = 8;
constexpr size_t kPrefixCount = 1000;
constexpr uint32_t kDirectoryOffset = kHeaderSize;
constexpr uint32_t kRecordsOffset = kDirectoryOffset + (kPrefixCount + 1) * sizeof(uint16_t);
constexpr float kCoordinateScale = 100.0f;  // Records hold hundredths of a degree.

uint16_t readUint16(const uint8_t* bytes) {
  return static_cast<uint16_t>(bytes[0]) |
         static_cast<uint16_t>(static_cast<uint16_t>(bytes[1]) << 8);
}

float readCoordinate(const uint8_t* bytes) {
  return static_cast<int16_t>(readUint16(bytes)) / kCoordinateScale;
}

// Reads `count` digits of an already-validated ZIP code as a number.
uint16_t readDigits(const char* digits, size_t count) {
  uint16_t value = 0;
  for (size_t index = 0; index < count; ++index) {
    value = static_cast<uint16_t>((value * 10) + (digits[index] - '0'));
  }
  return value;
}

// Reads the binary ZIP table: validates the header once, resolves a three-digit
// prefix to a record range through the directory, then scans only that range for
// the two-digit suffix. Owns the open file for the life of one lookup, which is
// what keeps the layout from leaking past this translation unit.
class ZipcodeTable {
 public:
  ~ZipcodeTable() {
    if (file_) file_.close();
  }

  bool open(const char* path);
  bool findLocation(const char* zipcode, ZipcodeLocation* location);

 private:
  bool readAt(uint32_t offset, uint8_t* buffer, size_t length);
  bool readHeader(const char* path);
  bool readPrefixRange(uint16_t prefix, uint16_t* start, uint16_t* end);

  File file_;                 // Open table file; closed with this object.
  uint16_t recordCount_ = 0;  // Records the header declares; bounds every seek.
};

bool ZipcodeTable::readAt(uint32_t offset, uint8_t* buffer, size_t length) {
  // File::read reports a short or failed read as a smaller (or negative) count,
  // so compare as signed rather than widening it into the requested length.
  return file_.seek(offset, SeekSet) &&
         (file_.read(buffer, length) == static_cast<int>(length));
}

bool ZipcodeTable::readHeader(const char* path) {
  uint8_t header[kHeaderSize];
  if (!readAt(0, header, sizeof(header))) {
    LOG_PRINTF("Zipcode table unreadable: %s", path);
    return false;
  }
  if (memcmp(header, kMagic, kMagicLength) != 0) {
    LOG_PRINTF("Zipcode table is not a ZIPB file: %s", path);
    return false;
  }
  if ((header[4] != kFormatVersion) || (header[5] != kRecordSize)) {
    LOG_PRINTF("Zipcode table version %u/%u unsupported, expected %u/%u",
               header[4], header[5],
               static_cast<unsigned>(kFormatVersion),
               static_cast<unsigned>(kRecordSize));
    return false;
  }

  recordCount_ = readUint16(header + 6);
  const uint32_t expectedSize = kRecordsOffset + (recordCount_ * kRecordSize);
  if (file_.size() != expectedSize) {
    LOG_PRINTF("Zipcode table truncated: %s is %u bytes, expected %u",
               path, static_cast<unsigned>(file_.size()),
               static_cast<unsigned>(expectedSize));
    return false;
  }
  return true;
}

bool ZipcodeTable::open(const char* path) {
  file_ = STORAGE.open(path, "r");
  if (!file_) {
    LOG_PRINTF("Zipcode table not found: %s", path);
    return false;
  }
  return readHeader(path);
}

bool ZipcodeTable::readPrefixRange(uint16_t prefix, uint16_t* start, uint16_t* end) {
  uint8_t entries[2 * sizeof(uint16_t)];
  if (!readAt(kDirectoryOffset + (prefix * sizeof(uint16_t)), entries, sizeof(entries))) {
    LOG_PRINTF("Zipcode directory unreadable at prefix %u", prefix);
    return false;
  }

  *start = readUint16(entries);
  *end = readUint16(entries + sizeof(uint16_t));
  if ((*start > *end) || (*end > recordCount_)) {
    LOG_PRINTF("Zipcode directory corrupt at prefix %u: [%u,%u) of %u",
               prefix, *start, *end, recordCount_);
    return false;
  }
  return true;
}

bool ZipcodeTable::findLocation(const char* zipcode, ZipcodeLocation* location) {
  const uint16_t prefix = readDigits(zipcode, kPrefixLength);
  const uint8_t suffix =
      static_cast<uint8_t>(readDigits(zipcode + kPrefixLength, kZipcodeLength - kPrefixLength));

  uint16_t start = 0;
  uint16_t end = 0;
  if (!readPrefixRange(prefix, &start, &end)) return false;

  // Seek once, then read forward: the bucket holds at most 100 records and its
  // bytes are contiguous, so the whole scan stays inside one filesystem block.
  if ((start != end) && !file_.seek(kRecordsOffset + (start * kRecordSize), SeekSet)) {
    LOG_PRINTF("Zipcode records unreadable at index %u", start);
    return false;
  }

  uint8_t record[kRecordSize];
  for (uint16_t index = start; index < end; ++index) {
    if (file_.read(record, sizeof(record)) != static_cast<int>(sizeof(record))) {
      LOG_PRINTF("Zipcode record %u unreadable", index);
      return false;
    }
    if (record[0] != suffix) continue;

    location->latitude = readCoordinate(record + 1);
    location->longitude = readCoordinate(record + 3);
    return true;
  }
  return false;
}

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

bool zipcodeLookupLocation(const char* zipcode, ZipcodeLocation* location, const char* path) {
  if ((location == nullptr) || (path == nullptr) || !isValidZipcode(zipcode)) {
    LOG_PRINTF("Zipcode lookup invalid arguments: zip=\"%s\" location=%s path=\"%s\"",
               zipcode == nullptr ? "(null)" : zipcode,
               location == nullptr ? "null" : "set",
               path == nullptr ? "(null)" : path);
    return false;
  }

  if (!storageManager.ensureMounted("read zipcode table")) {
    return false;
  }

  ZipcodeTable table;
  if (!table.open(path) || !table.findLocation(zipcode, location)) {
    LOG_PRINTF("Zipcode lookup failed in %s: %s", path, zipcode);
    return false;
  }
  return true;
}
