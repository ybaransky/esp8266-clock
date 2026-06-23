# ESP8266 Clock — PCB Project Brief (Flux.ai)

## Overview

A desktop clock with three 4-digit 7-segment displays, a battery-backed RTC, WiFi, and a single push button. The firmware runs on a Wemos D1 Mini (ESP8266). Power comes from USB-C (5 V, power-only).

---

## Components

Modules attached via headers (removable):

| Ref | Part | Attachment |
|-----|------|------------|
| U1  | Wemos D1 Mini v4 (or Lolin D1 Mini) | 2×8 female pin headers on PCB; male headers on module |
| U2  | DS3231 RTC module | Female pin headers on PCB; male headers on module |
| DSP1 | TM1637 4-digit 7-segment display module — left | 4-pin female headers on PCB |
| DSP2 | TM1637 4-digit 7-segment display module — center | 4-pin female headers on PCB |
| DSP3 | TM1637 4-digit 7-segment display module — right | 4-pin female headers on PCB |

Components soldered directly to PCB:

| Ref | Part | Description |
|-----|------|-------------|
| SW1 | uxcell 12×12×5 mm tactile push button | Through-hole, 4-pin |
| J1  | USB-C receptacle, power-only | Board power input |
| R1  | 5.1 kΩ resistor | USB-C CC1 pull-down |
| R2  | 5.1 kΩ resistor | USB-C CC2 pull-down |

---

## Pin Assignments

Authoritative source: `src/hardware.h`

| D1 Mini Pin | GPIO   | Net / Signal       | Notes |
|-------------|--------|--------------------|-------|
| D0          | GPIO16 | TM1637_DIO2        | No interrupt support; fine for TM1637 DIO |
| D1          | GPIO5  | I2C_SCL            | Hardware I2C — DS3231 SCL |
| D2          | GPIO4  | I2C_SDA            | Hardware I2C — DS3231 SDA |
| D3          | GPIO0  | BUTTON             | INPUT_PULLUP; must be HIGH at boot — do not pull low |
| D4          | GPIO2  | TM1637_DIO0 / LED  | Shared with on-board LED; idles HIGH (boot strapping pin) |
| D5          | GPIO14 | TM1637_CLK         | Shared CLK line for all three displays |
| D6          | GPIO12 | TM1637_DIO1        | |
| D7          | GPIO13 | RTC_SQW            | RISING interrupt, INPUT_PULLUP, 1 Hz square wave |
| D8          | GPIO15 | — (do not connect) | **Must be LOW at boot** — strapping pin; any pull-up prevents boot/flash |

---

## Net List

### I2C Bus (DS3231 RTC)
| Signal | From | To |
|--------|------|----|
| I2C_SCL | U1 D1 | U2 SCL |
| I2C_SDA | U1 D2 | U2 SDA |

### RTC SQW Interrupt
| Signal | From | To |
|--------|------|----|
| RTC_SQW | U2 SQW | U1 D7 |

### TM1637 Displays — shared CLK
| Signal | From | To |
|--------|------|----|
| TM1637_CLK | U1 D5 | DSP1 CLK, DSP2 CLK, DSP3 CLK |

### TM1637 Displays — individual DIO lines
| Signal | From | To |
|--------|------|----|
| TM1637_DIO0 | U1 D4 | DSP1 DIO |
| TM1637_DIO1 | U1 D6 | DSP2 DIO |
| TM1637_DIO2 | U1 D0 | DSP3 DIO |

### Button (uxcell 12×12×5 mm tactile, through-hole 4-pin)
| Signal | From | To |
|--------|------|----|
| BUTTON | U1 D3 | SW1 pin 1 (and pin 3 — bridged internally) |
| GND    | GND rail | SW1 pin 2 (and pin 4 — bridged internally) |

---

## Power Distribution

```
USB-C J1
  VBUS  ──► 5V rail ──► U1 5V pin
                    ──► DSP1, DSP2, DSP3 VCC
  GND   ──► GND rail ──► U1 GND, DSP1/2/3 GND, U2 GND, SW1
  CC1   ──► R1 (5.1 kΩ) ──► GND
  CC2   ──► R2 (5.1 kΩ) ──► GND
  D+, D- ──► not connected

U1 3V3 pin ──► U2 VCC  (if DS3231 module is 3.3 V)
               or
5V rail    ──► U2 VCC  (if DS3231 module accepts 5 V)
```

> **Note:** The CC pull-downs (5.1 kΩ to GND on both CC1 and CC2) are required to allow USB-C chargers to supply 5 V. Without them many USB-C sources will not provide power.

---

## Design Constraints

- **GPIO15 (D8) must be LOW at boot.** Do not route, do not add a pull-up. Leave the pad unconnected or tie directly to GND if needed for mechanical reasons.
- **GPIO0 (D3) must be HIGH at boot.** The INPUT_PULLUP in firmware handles this; do not add an external pull-down.
- **GPIO2 (D4) must be HIGH at boot.** The TM1637 DIO line idles HIGH and the LED is active-low, so both naturally satisfy this constraint.
- **TM1637 CLK is shared.** All three display modules connect to the same CLK net (U1 D5). Each has its own DIO line.
- **DS3231 SQW** requires INPUT_PULLUP (handled in firmware). A 4.7 kΩ pull-up to 3.3 V on the PCB is optional but acceptable.
- **I2C pull-ups:** If the DS3231 module does not include on-board pull-ups, add 4.7 kΩ resistors from SDA and SCL to 3.3 V.

---

## Suggested Board Layout Notes

- Place U1 (D1 Mini) centrally — all signals radiate from it. Use 2×8 female pin headers.
- Place U2 (DS3231) close to U1 to keep I2C traces short. Use female pin headers matching the module footprint.
- Place DSP1, DSP2, DSP3 in a horizontal row; their left-to-right order maps to DIO0 (D4), DIO1 (D6), DIO2 (D0). Use 4-pin female headers per display (VCC, GND, DIO, CLK).
- Place SW1 (12×12×5 mm tactile, through-hole) on an accessible edge or top face. Footprint: 12×12 mm body, 5 mm actuator height, standard 4-pin through-hole.
- Place J1 (USB-C) on the board edge, centered or on a short side.
- Keep CLK trace to all three displays roughly equal length to reduce skew (not critical at TM1637 speeds, but good practice).
- All header footprints should use 2.54 mm pitch (standard 0.1").
