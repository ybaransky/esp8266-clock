#pragma once

#include <Arduino.h>

namespace Hardware {
	namespace Pins {
		constexpr uint8_t I2C_SDA = D2;
		constexpr uint8_t I2C_SCL = D1;

		constexpr uint8_t BUTTON_1 = D8;
		constexpr uint8_t INTERNAL_LED = D4;
		constexpr uint8_t RTC_SQW = D7;
	}  // namespace Pins

	namespace I2CAddress {
		constexpr uint8_t DS3231 = 0x68;
	}  // namespace I2CAddress
}  // namespace Hardware

// ---------------------------------------------------------------------------
// I2C bus scanner
// ---------------------------------------------------------------------------

struct I2CScanResult {
	const uint8_t *addresses;
	size_t count;
};

class I2CBusScanner {
public:
	void scan();
	I2CScanResult lastResult() const;

private:
	static constexpr uint8_t FIRST_VALID_ADDRESS = 1;
	static constexpr uint8_t LAST_VALID_ADDRESS = 126;
	static constexpr size_t ADDRESS_CAPACITY = LAST_VALID_ADDRESS;

	uint8_t lastScanAddresses_[ADDRESS_CAPACITY] = {};
	size_t lastScanCount_ = 0;
};

extern I2CBusScanner i2cBusScanner;