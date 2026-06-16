#pragma once

#include <Arduino.h>

/*
Function       | Pin | GPIO   | Notes
---------------|-----|--------|-------------------------------
TM1637 ALL CLK | D5  | GPIO14 | Shared across all 3 displays
TM1637 #3 DIO  | D6  | GPIO12 | Safe
TM1637 #2 DIO  | D4  | GPIO2  | Boot strap pin; TM1637 DIO should idle HIGH
TM1637 #1 DIO  | D0  | GPIO16 | No interrupts, but fine for DIO
DS3231 SDA     | D2  | GPIO4  | Hardware I2C
DS3231 SCL     | D1  | GPIO5  | Hardware I2C
DS3231 SQW     | D7  | GPIO13 | Interrupt capable, INPUT_PULLUP
Button         | D3  | GPIO0  | INPUT_PULLUP, don't press at boot
D8             | D8  | GPIO15 | Unused; must stay LOW at boot
*/

namespace Hardware {
	namespace Pins {
		constexpr uint8_t I2C_SDA        = D2;
		constexpr uint8_t I2C_SCL        = D1;
		constexpr uint8_t BUTTON         = D3;
		constexpr uint8_t INTERNAL_LED   = D4;
		constexpr uint8_t RTC_SQW        = D7;
		constexpr uint8_t SEGMENT_DIO[3] = {D6, D4, D0}; /* DIO[0] = D6, DIO[1] = D4 */
		constexpr uint8_t SEGMENT_CLK    = D5;
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

	static const char *deviceNameForAddress(uint8_t address);

	uint8_t lastScanAddresses_[ADDRESS_CAPACITY] = {};
	size_t lastScanCount_ = 0;
};

extern I2CBusScanner i2cBusScanner;
