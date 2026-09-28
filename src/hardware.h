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
Buzzer         | D8  | GPIO15 | Must stay LOW at boot; drive through the
                                inverting NPN buffer in WIRING.md. The buzzer
                                module cannot sit on D8 directly - it idles its
                                S pin at 2.7V, which violates the strap.
*/

namespace Hardware {
	namespace Pins {
		constexpr uint8_t I2C_SCL        = D1;  // DS3231 clock (hardware I2C).
		constexpr uint8_t I2C_SDA        = D2;  // DS3231 data (hardware I2C).
		constexpr uint8_t BUTTON         = D3;  // Active-low; do not hold at boot (GPIO0 strap).
		constexpr uint8_t INTERNAL_LED   = D4;  // Active-low on-board LED; shares the pin with DIO0.
		constexpr uint8_t DIO0           = D4;  // Left TM1637 data.

		constexpr uint8_t DIO2           = D0;  // Right TM1637 data; GPIO16 has no interrupts, fine for DIO.
		constexpr uint8_t SEGMENT_CLK    = D5;  // TM1637 clock, shared by all three panels.
		constexpr uint8_t DIO1           = D6;  // Middle TM1637 data.
		constexpr uint8_t RTC_SQW        = D7;  // DS3231 1 Hz square wave, RISING interrupt.
		constexpr uint8_t BUZZER         = D8;  // Via inverting NPN; see WIRING.md.
		//                               left/top     right/bottom
		constexpr uint8_t SEGMENT_DIO[3] = {DIO0, DIO1, DIO2};
	}  // namespace Pins

	namespace I2CAddress {
		constexpr uint8_t DS3231 = 0x68;  // Fixed 7-bit address of the RTC.
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
	static constexpr uint8_t kFirstValidAddress = 1;  // First non-reserved address scanned.
	static constexpr uint8_t kLastValidAddress = 126;  // Last non-reserved address scanned.

	static const char *deviceNameForAddress(uint8_t address);
};

extern I2CBusScanner i2cBusScanner;  // Shared scanner used by boot diagnostics.

void printDeviceInfo();
