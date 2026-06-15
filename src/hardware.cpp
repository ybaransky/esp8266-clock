#include "hardware.h"

#include <Arduino.h>
#include <Wire.h>

class I2CBusScanner {
public:
  void scan() {
    Serial.println("Scanning I2C bus...");
    size_t found = 0;
    lastScanCount = 0;

    for (uint8_t address = FIRST_VALID_ADDRESS; address <= LAST_VALID_ADDRESS; address++) {
      Wire.beginTransmission(address);
      if (Wire.endTransmission() == 0) {
        Serial.printf("  Found device at 0x%02X\n", address);
        if (found < ADDRESS_CAPACITY) {
          lastScanAddresses[found] = address;
        }
        found++;
      }
    }

    lastScanCount = (found < ADDRESS_CAPACITY) ? found : ADDRESS_CAPACITY;
    Serial.printf("Scan complete. %u device(s) found.\n\n", static_cast<unsigned>(found));
  }

  I2CScanResult lastResult() const {
    return {lastScanAddresses, lastScanCount};
  }

private:
  static constexpr uint8_t FIRST_VALID_ADDRESS = 1;
  static constexpr uint8_t LAST_VALID_ADDRESS = 126;
  static constexpr size_t ADDRESS_CAPACITY = LAST_VALID_ADDRESS;

  uint8_t lastScanAddresses[ADDRESS_CAPACITY] = {};
  size_t  lastScanCount = 0;
};

static I2CBusScanner i2cBusScanner;

void i2cScan() {
  i2cBusScanner.scan();
}

I2CScanResult i2cGetLastScanResult() {
  return i2cBusScanner.lastResult();
}
