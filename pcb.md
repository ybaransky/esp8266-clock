# ESP8266 Clock PCB Specification

## Project Information

Project Name: ESP8266 Clock
Revision: 0.1

## Controller

Board:
- [ ] Wemos D1 Mini v4
- [ ] Lolin D1 Mini
- [ ] Other: __________

Mounting:
- [ ] Female headers
- [ ] Male headers
- [ ] Soldered directly

## RTC

Type:
- [ ] DS3231 Module
- [ ] Bare DS3231 IC

SQW Connected:
- [ ] Yes
- [ ] No

Battery:
- [ ] CR2032 Holder on PCB
- [ ] RTC Module Battery
- [ ] None

## Displays

Number of Displays: 3

Display Type:
- [ ] TM1637 Module
- [ ] Bare LED Display

Shared CLK:
- [ ] Yes

Display Positions:

Display 1:
Location: __________

Display 2:
Location: __________

Display 3:
Location: __________

## Button

Type:
- [ ] 6x6mm Tact Switch
- [ ] Other

Location:
_________________

## Power

Source:
- [x] USB-C power input on PCB
- [ ] USB through D1 Mini
- [ ] External 5V
- [ ] External 3.3V
- [ ] Battery

USB-C Input:
- Connector Type: USB-C receptacle, power-only
- Output Voltage: 5V
- USB Data Lines: Not connected
- Required Current: 1A minimum preferred

USB-C Wiring:
- VBUS -> PCB 5V rail
- GND -> PCB GND
- CC1 -> 5.1k resistor -> GND
- CC2 -> 5.1k resistor -> GND
- D+ -> not connected
- D- -> not connected

Power Distribution:
- PCB 5V rail -> D1 Mini 5V pin
- PCB GND -> D1 Mini GND
- TM1637 modules powered from 5V rail
- DS3231 powered from 3.3V or 5V, depending on module

_________________

## PCB

Width (mm):
Height (mm):

Layers:
- [ ] 2 Layer
- [ ] 4 Layer

Board Thickness:
- [ ] 1.6 mm
- [ ] Other: ______

Copper Weight:
- [ ] 1 oz
- [ ] 2 oz

## Mounting

Mounting Holes:
- [ ] None
- [ ] 2
- [ ] 4

Hole Diameter:
_______ mm

Locations:
_________________

## Connectors

Connector 1:
Purpose:
Pins:

Connector 2:
Purpose:
Pins:

## Silkscreen

Text to appear on board:

_________________

## Notes

_________________

## Pin Map

| Pin | GPIO   | Connection           |
|-----|--------|----------------------|
| D1  | GPIO5  | DS3231 SCL           |
| D2  | GPIO4  | DS3231 SDA           |
| D5  | GPIO14 | DS3231 SQW           |
| D6  | GPIO12 | TM1637 CLK (shared)  |
| D7  | GPIO13 | TM1637 DIO #1        |
| D0  | GPIO16 | TM1637 DIO #2        |
| D8  | GPIO15 | TM1637 DIO #3        |
| D3  | GPIO0  | Button Interrupt     |