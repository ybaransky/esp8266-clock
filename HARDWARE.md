# ESP8266 Clock — PCB Specification

Board specification and fabrication brief for the desktop clock. **`src/hardware.h`
is the authoritative pin map**; this document must follow it, never the reverse.
The buzzer's transistor buffer has its own document, [WIRING.md](WIRING.md),
because it is a circuit rather than a net.

Revision 0.1 — pin assignments complete, fabrication parameters open (see
[Open decisions](#open-decisions)).

## Overview

A desktop clock with three 4-digit 7-segment displays, a battery-backed RTC,
WiFi, a single push button, and a buzzer. The firmware runs on a Wemos D1 Mini
(ESP8266). Power comes from USB-C (5 V, power-only — no data lines).

Every usable GPIO is allocated. There are no spare pins, which is why the buzzer
needs a transistor on a strapping pin rather than a free one.

## Bill of materials

Modules attached via 2.54 mm header, removable:

| Ref | Part | Attachment |
|-----|------|------------|
| U1  | Wemos D1 Mini v4 (or Lolin D1 Mini) | 2×8 female headers on PCB; male on module |
| U2  | DS3231 RTC module, with its own coin cell | Female headers on PCB; male on module |
| DSP1 | TM1637 4-digit 7-segment module — left | 4-pin female header (VCC, GND, DIO, CLK) |
| DSP2 | TM1637 4-digit 7-segment module — center | 4-pin female header |
| DSP3 | TM1637 4-digit 7-segment module — right | 4-pin female header |
| BZ1 | KY-006 style active buzzer module, active LOW | 3-pin female header (VCC, GND, S) |

Soldered directly to the PCB:

| Ref | Part | Purpose |
|-----|------|---------|
| SW1 | uxcell 12×12×5 mm tactile push button, through-hole 4-pin | User button |
| J1  | USB-C receptacle, power-only | Board power input |
| R1  | 5.1 kΩ | USB-C CC1 pull-down |
| R2  | 5.1 kΩ | USB-C CC2 pull-down |
| Q1  | 2N2222, PN2222, BC547 or S8050 NPN | Inverting buffer for the buzzer |
| R3  | 1 kΩ | Q1 base resistor |
| R4  | 10 kΩ | Q1 base pull-down — **mandatory**, holds GPIO15 LOW at reset |
| RV1 | 1 kΩ–10 kΩ linear potentiometer | *Optional* hardware volume; see WIRING.md |

A 2N7000 MOSFET substitutes for Q1: gate to D8, drain to BZ1 S, source to GND,
R4 from gate to GND, and R3 omitted.

> **RV1 and firmware volume must not stack.** Fitting the potentiometer means
> setting volume to 100% on `/sound` (50% PWM duty).

## Pin assignments

Mirrors `src/hardware.h`. DIO indices are 0-based to match `SEGMENT_DIO[3]` in
firmware; DSP1/2/3 are left-to-right as mounted.

| D1 Mini | GPIO | Net | Notes |
|---------|------|-----|-------|
| D0 | GPIO16 | `TM1637_DIO2` | No interrupt support; fine for DIO |
| D1 | GPIO5  | `I2C_SCL` | Hardware I2C — DS3231 SCL |
| D2 | GPIO4  | `I2C_SDA` | Hardware I2C — DS3231 SDA |
| D3 | GPIO0  | `BUTTON` | `INPUT_PULLUP`; **must be HIGH at boot** |
| D4 | GPIO2  | `TM1637_DIO0` | Shared with the on-board LED; **must be HIGH at boot** |
| D5 | GPIO14 | `TM1637_CLK` | Shared by all three displays |
| D6 | GPIO12 | `TM1637_DIO1` | Unconstrained |
| D7 | GPIO13 | `RTC_SQW` | RISING interrupt, `INPUT_PULLUP`, 1 Hz |
| D8 | GPIO15 | `BUZZER_DRIVE` | **Must be LOW at boot.** To Q1 base via R3, never to BZ1 directly |

The internal LED shares D4 with `TM1637_DIO0` and is active-low, so it flickers
during display writes. That is expected and needs no board-level fix.

## Net list

### I2C (DS3231)

| Net | From | To |
|-----|------|----|
| `I2C_SCL` | U1 D1 | U2 SCL |
| `I2C_SDA` | U1 D2 | U2 SDA |

### RTC square wave

| Net | From | To |
|-----|------|----|
| `RTC_SQW` | U2 SQW | U1 D7 |

### Displays — shared clock

| Net | From | To |
|-----|------|----|
| `TM1637_CLK` | U1 D5 | DSP1 CLK, DSP2 CLK, DSP3 CLK |

### Displays — individual data lines

| Net | From | To |
|-----|------|----|
| `TM1637_DIO0` | U1 D4 | DSP1 DIO |
| `TM1637_DIO1` | U1 D6 | DSP2 DIO |
| `TM1637_DIO2` | U1 D0 | DSP3 DIO |

### Button

The uxcell 4-pin tactile switch bridges pins 1–3 and 2–4 internally.

| Net | From | To |
|-----|------|----|
| `BUTTON` | U1 D3 | SW1 pin 1 (and 3) |
| `GND` | GND rail | SW1 pin 2 (and 4) |

### Buzzer

D8 cannot drive BZ1 directly: the module idles its S pin at 2.7 V, which
violates the GPIO15 strap and prevents boot. Q1 inverts D8 instead — see
[WIRING.md](WIRING.md) for the full schematic and reasoning.

| Net | From | To |
|-----|------|----|
| `BUZZER_DRIVE` | U1 D8 | R3 (1 kΩ) to Q1 base |
| `BUZZER_STRAP` | Q1 base | R4 (10 kΩ) to GND |
| `BUZZER_SINK` | Q1 collector | BZ1 S |
| `GND` | Q1 emitter | GND rail |
| `3V3` | U1 3V3 | BZ1 VCC |

`D8 HIGH` turns Q1 on, pulls BZ1 S low, and the buzzer sounds. `D8 LOW` is silent.

## Power

Source: USB-C power input on the PCB, not through the D1 Mini's own USB.
**1 A minimum preferred** — three TM1637 panels plus WiFi transmit peaks.

```text
USB-C J1
  VBUS  ──► 5V rail ──► U1 5V pin
                    ──► DSP1, DSP2, DSP3 VCC
  GND   ──► GND rail ──► U1, DSP1/2/3, U2, BZ1, Q1 emitter, SW1
  CC1   ──► R1 (5.1 kΩ) ──► GND
  CC2   ──► R2 (5.1 kΩ) ──► GND
  D+/D- ──► not connected

U1 3V3 ──► BZ1 VCC
       ──► U2 VCC   (if the DS3231 module is 3.3 V)
5V rail ─► U2 VCC   (if the DS3231 module accepts 5 V)
```

> **The CC pull-downs are not optional.** Without 5.1 kΩ from both CC1 and CC2
> to GND, many USB-C sources supply no power at all.

Do not judge WiFi behavior while the board is powered from a PC's USB port —
supply droop plus a nearby 2.4 GHz radio degrades transfers severely with
perfectly healthy firmware. CLAUDE.md records the measurements.

## Boot-strap constraints

The ESP8266 samples three pins at reset. Violating any of them means the board
does not boot, with no error to read.

| Pin | Required at reset | What satisfies it |
|-----|-------------------|-------------------|
| GPIO15 (D8) | **LOW** | R4, the 10 kΩ base pull-down. Nothing else on this net may source current |
| GPIO0 (D3) | **HIGH** | Firmware `INPUT_PULLUP` plus the button being open. Add no external pull-down |
| GPIO2 (D4) | **HIGH** | TM1637 DIO idles HIGH and the LED is active-low; both satisfy it naturally |

Do not press SW1 while the board resets — that pulls GPIO0 low and enters flash
mode.

## Optional passives

- **DS3231 SQW**: firmware uses `INPUT_PULLUP`. A 4.7 kΩ pull-up to 3.3 V on the
  PCB is acceptable but unnecessary.
- **I2C**: if the DS3231 module omits on-board pull-ups, add 4.7 kΩ from SDA and
  SCL to 3.3 V.

## Layout notes

- **U1 central** — every signal radiates from it.
- **U2 close to U1**, keeping I2C traces short.
- **DSP1 through DSP3 in a horizontal row**, left to right = DIO0 (D4),
  DIO1 (D6), DIO2 (D0). Getting this order wrong swaps panels with no
  electrical symptom to catch it.
- **Q1, R3 and R4 close to U1's D8 pin.** R4 is what holds the strap; a long
  trace between D8 and R4 is antenna on a pin that must be quiet at reset.
- **BZ1 wherever it is audible**, but keep its wiring away from the I2C pair.
- **SW1** on an accessible edge or the top face. 12×12 mm body, 5 mm actuator,
  standard 4-pin through-hole.
- **J1** on a board edge.
- Keep `TM1637_CLK` roughly equal-length to all three panels. Not critical at
  TM1637 speeds, but free to do.
- All headers 2.54 mm pitch.

## Open decisions

Nothing in this section is settled. These are the fabrication parameters that
need answers before ordering; the electrical design above does not depend on
any of them.

| Parameter | Options / units | Chosen |
|-----------|-----------------|--------|
| Board width × height | mm | — |
| Layer count | 2 or 4 | — |
| Board thickness | 1.6 mm or other | — |
| Copper weight | 1 oz or 2 oz | — |
| Mounting holes | none / 2 / 4 | — |
| Mounting hole diameter | mm | — |
| Mounting hole positions | — | — |
| Display physical positions | enclosure-dependent | — |
| Button position | edge or top face | — |
| Additional connectors | purpose and pin count | — |
| Silkscreen text | — | — |

Two choices are already made, stated here so they are not reopened by mistake:
the controller is a Wemos D1 Mini v4 or Lolin D1 Mini on **female headers**, so
it stays removable, and the RTC is a **DS3231 module carrying its own coin
cell**, not a bare IC with a CR2032 holder on the PCB.
