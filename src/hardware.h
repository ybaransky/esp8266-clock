#pragma once

#include <Arduino.h>

#ifdef ALT_PINMAP

+------------+--------+----------------------+-------------------------------------------+
| D1 Mini    | GPIO   | Function             | Notes                                     |
+------------+--------+----------------------+-------------------------------------------+
| D1         | GPIO5  | DS3231 SCL           | I2C clock, safe boot pin                  |
| D2         | GPIO4  | DS3231 SDA           | I2C data, safe boot pin                   |
| D5         | GPIO14 | DS3231 SQW           | Interrupt-capable, safe at boot           |
| D6         | GPIO12 | TM1637 CLK           | Shared clock for all 3 TM1637 displays    |
| D7         | GPIO13 | TM1637 DIO #1        | Display #1 data                           |
| D0         | GPIO16 | TM1637 DIO #2        | Display #2 data                           |
| D8         | GPIO15 | TM1637 DIO #3        | Display #3 data, must remain LOW at boot  |
| D3         | GPIO0  | Button Interrupt     | INPUT_PULLUP, must be HIGH at boot        |
| D4         | GPIO2  | Unused               | Internal LED, left unused                 |
| RX         | GPIO3  | Unused               | Preserves serial/programming              |
| TX         | GPIO1  | Unused               | Preserves serial/programming              |
| A0         | ADC    | Unused               | Analog input not used                     |
+------------+--------+----------------------+-------------------------------------------+

Boot Requirements
-----------------
D3 (GPIO0)  : HIGH during boot
D4 (GPIO2)  : HIGH during boot
D8 (GPIO15) : LOW during boot

Button Wiring
-------------
3.3V
 |
 +--[internal pullup]--> GPIO0 (D3)
 |
 +---- Button ---- GND

Code:
    pinMode(D3, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(D3), buttonISR, FALLING);

TM1637 Connections
------------------
Display #1: CLK=D6, DIO=D7
Display #2: CLK=D6, DIO=D0
Display #3: CLK=D6, DIO=D8

DS3231 Connections
------------------
SDA  -> D2 (GPIO4)
SCL  -> D1 (GPIO5)
SQW  -> D5 (GPIO14)
VCC  -> 3.3V
GND  -> GND
+----------------+---------+--------+--------------------------------------+
| Function       | D1 Mini | GPIO   | Notes                                |
+----------------+---------+--------+--------------------------------------+
| RTC SDA        | D2      | GPIO4  | DS3231 I2C SDA                       |
| RTC SCL        | D1      | GPIO5  | DS3231 I2C SCL                       |
| RTC SQW        | D5      | GPIO14 | Interrupt input from DS3231 SQW      |
| Button         | D6      | GPIO12 | Interrupt input, INPUT_PULLUP        |
| TM1637 CLK     | D0      | GPIO16 | Shared clock for all 3 displays      |
| TM1637 #1 DIO  | D7      | GPIO13 | Display 1 DIO                        |
| TM1637 #2 DIO  | RX      | GPIO3  | Display 2 DIO                        |
| TM1637 #3 DIO  | TX      | GPIO1  | Display 3 DIO                        |
+----------------+---------+--------+--------------------------------------+

Boot-Strap Pins (DO NOT USE)
+-----------+--------+--------------------------------------+
| D1 Mini   | GPIO   | Reason                               |
+-----------+--------+--------------------------------------+
| D3        | GPIO0  | Must be HIGH during boot            |
| D4        | GPIO2  | Built-in LED, must be HIGH at boot  |
| D8        | GPIO15 | Must be LOW during boot             |
+-----------+--------+--------------------------------------+
#endif



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
	const uint8_t *addresses;  // Addresses found during the last scan.
	size_t count;              // Number of valid entries in addresses.
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

	uint8_t lastScanAddresses_[ADDRESS_CAPACITY] = {};  // Cached scan addresses.
	size_t lastScanCount_ = 0;                           // Cached address count.
};

extern I2CBusScanner i2cBusScanner;

void printDeviceInfo();
