#include "hardware.h"

#include <Arduino.h>
#include <Wire.h>

namespace {

struct I2CDeviceAddressLabel {
  uint8_t address;
  const char *name;
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

}  // namespace

const char *I2CBusScanner::deviceNameForAddress(uint8_t address) {
  for (const I2CDeviceAddressLabel &device : KNOWN_I2C_DEVICES) {
    if (device.address == address) {
      return device.name;
    }
  }
  return "unknown";
}

void I2CBusScanner::scan() {
  Serial.println("[I2C] Scanning I2C bus...");
  size_t found = 0;
  lastScanCount_ = 0;

  for (uint8_t address = FIRST_VALID_ADDRESS; address <= LAST_VALID_ADDRESS; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf("[I2C] Found device at 0x%02X: (%s)\n", address, deviceNameForAddress(address));
      if (found < ADDRESS_CAPACITY) {
        lastScanAddresses_[found] = address;
      }
      found++;
    }
  }

  lastScanCount_ = (found < ADDRESS_CAPACITY) ? found : ADDRESS_CAPACITY;
  Serial.printf("[I2C] Scan complete. %u device(s) found.\n", static_cast<unsigned>(found));
}

I2CScanResult I2CBusScanner::lastResult() const {
  return {lastScanAddresses_, lastScanCount_};
}

I2CBusScanner i2cBusScanner;
