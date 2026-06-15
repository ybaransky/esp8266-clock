#include "hardware.h"

#include <Arduino.h>
#include <Wire.h>

void I2CBusScanner::scan() {
  Serial.println("Scanning I2C bus...");
  size_t found = 0;
  lastScanCount_ = 0;

  for (uint8_t address = FIRST_VALID_ADDRESS; address <= LAST_VALID_ADDRESS; address++) {
    Wire.beginTransmission(address);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  Found device at 0x%02X\n", address);
      if (found < ADDRESS_CAPACITY) {
        lastScanAddresses_[found] = address;
      }
      found++;
    }
  }

  lastScanCount_ = (found < ADDRESS_CAPACITY) ? found : ADDRESS_CAPACITY;
  Serial.printf("Scan complete. %u device(s) found.\n\n", static_cast<unsigned>(found));
}

I2CScanResult I2CBusScanner::lastResult() const {
  return {lastScanAddresses_, lastScanCount_};
}

I2CBusScanner i2cBusScanner;
