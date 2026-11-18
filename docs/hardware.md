# ESP8266 Clock — Hardware

**Authoritative pin assignments:** `src/hardware.h`. This document records the pin map and hardware reasoning; the table was checked against `src/hardware.h` on 2026-11-17. If they ever disagree, `src/hardware.h` wins. See [buzzer_wiring.md](buzzer_wiring.md) for the detailed buzzer circuit and [pcb-specification.md](pcb-specification.md) for the PCB BOM, nets, and fabrication brief. The Fritzing wiring diagram is [clock.fzz](clock.fzz); it has not been checked against `src/hardware.h`.

### Hardware pin map (authoritative source: `src/hardware.h`)

| Signal        | Pin | GPIO   | Notes                                                              |
|---------------|-----|--------|--------------------------------------------------------------------|
| *Left side*   |     |        |                                                                    |
| TM1637 DIO[2] | D0  | GPIO16 | No interrupts; fine for DIO                                        |
| TM1637 CLK    | D5  | GPIO14 | Shared across all 3 displays                                       |
| TM1637 DIO[1] | D6  | GPIO12 | Safe                                                               |
| DS3231 SQW    | D7  | GPIO13 | RISING interrupt, INPUT_PULLUP, 1Hz                                |
| Buzzer        | D8  | GPIO15 | **Strapping pin; must stay LOW at boot.** Inverting NPN buffer required — see [buzzer_wiring.md](buzzer_wiring.md) |
| *Right side*  |     |        |                                                                    |
| DS3231 SCL    | D1  | GPIO5  | Hardware I2C                                                       |
| DS3231 SDA    | D2  | GPIO4  | Hardware I2C                                                       |
| Button        | D3  | GPIO0  | INPUT_PULLUP, pressed = LOW; do not hold at boot                   |
| TM1637 DIO[0] | D4  | GPIO2  | Shares with INTERNAL_LED; both idle HIGH                           |
| Internal LED  | D4  | GPIO2  | Active-low; shared with TM1637 DIO[0] — LED flickers during write |

### Pin boot constraints
- **GPIO15 (D8)**: must be LOW - the buzzer's 10k base pulldown is what satisfies this. Any pull-up prevents boot/flash, which is why the active-LOW buzzer module (S idles at 2.7V) cannot sit on D8 directly. [buzzer_wiring.md](buzzer_wiring.md) explains the NPN buffer and why a plain pulldown does not work.
- **GPIO0 (D3)**: must be HIGH - INPUT_PULLUP + button not pressed.
- **GPIO2 (D4)**: must be HIGH - LED and TM1637 DIO both idle HIGH; safe.
