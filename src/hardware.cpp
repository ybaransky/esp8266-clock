#include "hardware.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Wire.h>
#include "log.h"
#include "number_format.h"
#include "storage_manager.h"

namespace {

struct I2CDeviceAddressLabel {
  uint8_t address;     // I2C address to label.
  const char *name;    // Human-readable device guess.
};

struct HeapInfo {
  uint32_t freeBytes;          // Current free heap.
  uint32_t maxFreeBlockBytes;  // Largest allocatable heap block.
  uint8_t fragmentationPct;    // ESP heap fragmentation percentage.
};

struct FlashInfo {
  uint32_t chipSizeBytes;  // Configured flash size.
  uint32_t realSizeBytes;  // Detected physical flash size.
  uint32_t speedHz;        // Flash bus speed.
  FlashMode_t mode;        // Flash access mode.
};

struct ChipInfo {
  uint32_t chipId;     // ESP8266 chip identifier.
  uint8_t cpuFreqMHz;  // CPU frequency.
  String sdkVersion;   // Espressif SDK version.
  String coreVersion;  // Arduino core version.
  String resetReason;  // Last reset reason.
};

struct SketchInfo {
  uint32_t sizeBytes;       // Current sketch size.
  uint32_t freeSpaceBytes;  // Free OTA/sketch space.
};

struct StorageInfo {
  bool mounted;       // True when LittleFS mounted.
  size_t totalBytes;  // LittleFS total capacity.
  size_t usedBytes;   // LittleFS used capacity.
};

constexpr I2CDeviceAddressLabel KNOWN_I2C_DEVICES[] = {
    {0x20, "PCF8574 GPIO expander / LCD backpack"},
    {0x21, "PCF8574 GPIO expander / LCD backpack"},
    {0x22, "PCF8574 GPIO expander / LCD backpack"},
    {0x23, "PCF8574 GPIO expander / LCD backpack"},
    {0x24, "PCF8574 GPIO expander / LCD backpack"},
    {0x25, "PCF8574 GPIO expander / LCD backpack"},
    {0x26, "PCF8574 GPIO expander / LCD backpack"},
    {0x27, "PCF8574 GPIO expander / LCD backpack"},
    {0x3C, "SSD1306 OLED display"},
    {0x3D, "SSD1306 OLED display"},
    {0x40, "INA219 current sensor / HTU21D / Si7021"},
    {0x48, "ADS1115 ADC / TMP102 temperature sensor"},
    {0x49, "ADS1115 ADC / TMP102 temperature sensor"},
    {0x4A, "ADS1115 ADC / TMP102 temperature sensor"},
    {0x4B, "ADS1115 ADC / TMP102 temperature sensor"},
    {0x50, "AT24Cxx EEPROM"},
    {0x51, "AT24Cxx EEPROM"},
    {0x52, "AT24Cxx EEPROM"},
    {0x53, "AT24Cxx EEPROM"},
    {0x54, "AT24Cxx EEPROM"},
    {0x55, "AT24Cxx EEPROM"},
    {0x56, "AT24Cxx EEPROM"},
    {0x57, "AT24Cxx EEPROM / MAX3010x pulse oximeter"},
    {Hardware::I2CAddress::DS3231, "DS3231 RTC / DS1307 RTC / MPU6050 IMU"},
    {0x69, "MPU6050 IMU alternate address"},
    {0x76, "BME280 / BMP280 / BMP388 pressure sensor"},
    {0x77, "BME280 / BMP280 / BMP388 pressure sensor"},
};

HeapInfo getHeapInfo() {
  return {
    ESP.getFreeHeap(),
    ESP.getMaxFreeBlockSize(),
    ESP.getHeapFragmentation(),
  };
}

FlashInfo getFlashInfo() {
  return {
    ESP.getFlashChipSize(),
    ESP.getFlashChipRealSize(),
    ESP.getFlashChipSpeed(),
    ESP.getFlashChipMode(),
  };
}

ChipInfo getChipInfo() {
  return {
    ESP.getChipId(),
    static_cast<uint8_t>(ESP.getCpuFreqMHz()),
    ESP.getSdkVersion(),
    ESP.getCoreVersion(),
    ESP.getResetReason(),
  };
}

SketchInfo getSketchInfo() {
  return {
    ESP.getSketchSize(),
    ESP.getFreeSketchSpace(),
  };
}

StorageInfo getStorageInfo() {
  if (!storageManager.ensureMounted("device info")) return {false, 0, 0};
  FSInfo fs;
  if (!LittleFS.info(fs)) return {false, 0, 0};
  return {true, fs.totalBytes, fs.usedBytes};
}

const char* flashModeName(FlashMode_t mode) {
  switch (mode) {
    case FM_QIO:   return "QIO";
    case FM_QOUT:  return "QOUT";
    case FM_DIO:   return "DIO";
    case FM_DOUT:  return "DOUT";
    default:       return "unknown";
  }
}

}  // namespace

// -----------------------------------------------------------------------------
// I2CBusScanner
// -----------------------------------------------------------------------------

const char *I2CBusScanner::deviceNameForAddress(uint8_t address) {
  for (const I2CDeviceAddressLabel &device : KNOWN_I2C_DEVICES) {
    if (device.address == address) {
      return device.name;
    }
  }
  return "unknown";
}

void I2CBusScanner::scan() {
  LOG_PRINTLN("Scanning I2C bus...");
  size_t found = 0;
  lastScanCount_ = 0;

  for (uint8_t address = FIRST_VALID_ADDRESS; address <= LAST_VALID_ADDRESS; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      LOG_PRINTF("Found device at 0x%02X: (%s)\n", address, deviceNameForAddress(address));
      if (found < ADDRESS_CAPACITY) {
        lastScanAddresses_[found] = address;
      }
      found++;
    }
  }

  lastScanCount_ = (found < ADDRESS_CAPACITY) ? found : ADDRESS_CAPACITY;
  LOG_PRINTF("Scan complete. %u device(s) found.\n", static_cast<unsigned>(found));
}

I2CScanResult I2CBusScanner::lastResult() const {
  return {lastScanAddresses_, lastScanCount_};
}

I2CBusScanner i2cBusScanner;

void printDeviceInfo() {
  const HeapInfo    heap    = getHeapInfo();
  const FlashInfo   flash   = getFlashInfo();
  const ChipInfo    chip    = getChipInfo();
  const SketchInfo  sketch  = getSketchInfo();
  const StorageInfo storage = getStorageInfo();

  LOG_PRINTF("Heap: free=%s  maxBlock=%s  frag=%u%%\n",
             CommaNumber(heap.freeBytes).c_str(),
             CommaNumber(heap.maxFreeBlockBytes).c_str(),
             heap.fragmentationPct);

  LOG_PRINTF("Flash: chipSize=%s  realSize=%s  speed=%sHz  mode=%s\n",
             CommaNumber(flash.chipSizeBytes).c_str(),
             CommaNumber(flash.realSizeBytes).c_str(),
             CommaNumber(flash.speedHz).c_str(),
             flashModeName(flash.mode));

  LOG_PRINTF("Chip: id=0x%06X  cpu=%uMHz  sdk=%s  core=%s  resetReason=%s\n",
             chip.chipId,
             chip.cpuFreqMHz,
             chip.sdkVersion.c_str(),
             chip.coreVersion.c_str(),
             chip.resetReason.c_str());

  LOG_PRINTF("Sketch: size=%s  freeSpace=%s\n",
             CommaNumber(sketch.sizeBytes).c_str(),
             CommaNumber(sketch.freeSpaceBytes).c_str());

  if (!storage.mounted) {
    LOG_PRINTF("Storage: LittleFS not mounted\n");
  } else {
    LOG_PRINTF("Storage: total=%s  used=%s  free=%s\n",
               CommaNumber(storage.totalBytes).c_str(),
               CommaNumber(storage.usedBytes).c_str(),
               CommaNumber(storage.totalBytes - storage.usedBytes).c_str());
  }
}
