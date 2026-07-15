#pragma once

#include <Arduino.h>

/*
Function       | Pin | GPIO   | Notes
---------------|-----|--------|-------------------------------
DS3231 SCL     | D1  | GPIO5  | Hardware I2C
DS3231 SDA     | D2  | GPIO4  | Hardware I2C
Button         | D3  | GPIO0  | INPUT_PULLUP, don't press at boot
TM1637 DIO[0]  | D4  | GPIO2  | Boot strap pin; TM1637 DIO should idle HIGH
INTERNAL LED   | D4  | GPIO2  | gets dragged along

TM1637 DIO[2]  | D0  | GPIO16 | No interrupts, but fine for DIO
TM1637 ALL CLK | D5  | GPIO14 | Shared across all 3 displays
TM1637 DIO[1]  | D6  | GPIO12 | Safe
DS3231 SQW     | D7  | GPIO13 | Interrupt capable, INPUT_PULLUP
D8             | D8  | GPIO15 | Unused; must stay LOW at boot
*/

namespace Hardware {
	namespace Pins {
		constexpr uint8_t I2C_SCL        = D1;
		constexpr uint8_t I2C_SDA        = D2;
		constexpr uint8_t BUTTON         = D3;
		constexpr uint8_t INTERNAL_LED   = D4;
		constexpr uint8_t DIO0           = D4; 

		constexpr uint8_t DIO2           = D0; 
		constexpr uint8_t SEGMENT_CLK    = D5;
		constexpr uint8_t DIO1           = D6; 
		constexpr uint8_t RTC_SQW        = D7;
		//                               left/top     right/bottom
		constexpr uint8_t SEGMENT_DIO[3] = {DIO0, DIO1, DIO2};
	}  // namespace Pins

	namespace I2CAddress {
		constexpr uint8_t DS3231 = 0x68;
	}  // namespace I2CAddress
}  // namespace Hardware

// ---------------------------------------------------------------------------
// I2C bus scanner
// ---------------------------------------------------------------------------

// Probes the I2C address range and logs each responding device by name.
class I2CBusScanner {
public:
	void scan();

private:
	static constexpr uint8_t FIRST_VALID_ADDRESS = 1;  // First non-reserved address scanned.
	static constexpr uint8_t LAST_VALID_ADDRESS = 126;  // Last non-reserved address scanned.

	static const char *deviceNameForAddress(uint8_t address);
};

extern I2CBusScanner i2cBusScanner;

void printDeviceInfo();
