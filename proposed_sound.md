# Proposed design: boundary beeper for Friday and Trading modes

Status: **proposal** — not implemented. Written 2026-07-15.

## Goal

Sound a short beep when Friday or Trading mode crosses a phase boundary
**live** (Friday sunset arrives; market opens; market closes) — the same
moments that currently blink `messages.fridaySunset`, `messages.tradingOpen`,
and `messages.tradingClose` on the display. Boot, config reload, and browser
time sync must never produce a sound, for the same reason those events never
produce a boundary message.

## Pin choice: D8 / GPIO15

Every other GPIO is occupied: D0/D5/D6 drive the TM1637s, D1/D2 are I2C,
D3 is the button, D4 is the shared LED/DIO, D7 is the SQW interrupt, TX/RX
carry serial logging, and A0 is input-only. D8 is the only free pin — and it
is also the *right* pin for a buzzer:

- GPIO15 is a strapping pin that must be **LOW at boot**; any pull-up
  prevents boot/flash. A buzzer sits between the pin and **GND**, so it can
  never pull the pin up. The D1 mini's onboard 10k pull-down keeps the pin
  firmly low.
- GPIO15 idles LOW and does not toggle during the ROM boot sequence, so the
  buzzer stays **silent through boot and flashing**. (GPIO2/D4 would chirp on
  every reset; GPIO15 cannot.)

The `hardware.h` pin table note for D8 changes from "unused; must stay LOW at
boot" to "buzzer — low-side load only; nothing that pulls up." Add:

```cpp
constexpr uint8_t BUZZER = D8;  // GPIO15: strapping pin, load to GND only.
```

and update the pin tables in `hardware.h` and CLAUDE.md.

## Device: passive piezo transducer

|                | Active buzzer                          | Passive piezo (recommended)          |
|----------------|----------------------------------------|--------------------------------------|
| Drive          | `digitalWrite(HIGH)`                   | square wave via `tone()`             |
| Pitch          | fixed — one beep sound                 | any frequency → distinct per event   |
| Current        | 20-30 mA → needs transistor (GPIO ~12 mA max) | ~1-2 mA → direct GPIO drive   |
| Wiring         | transistor + base resistors            | one series resistor                  |

**Recommendation: a passive piezo buzzer** (12 mm piezo disc sold as
"passive", e.g. a Murata PKM22-class part or a KY-006-style module).

- **Wiring:** `D8 → ~220 Ω series resistor → piezo → GND`. No transistor, no
  flyback diode; the piezo element is capacitive and draws almost nothing.
- **Frequency:** drive at 2.7-4 kHz where piezo discs resonate loudest. At
  3.3 V direct drive this is a clear room-level alert.
- **Distinct tones:** three semantically different boundaries get three
  signatures, parallel to their three configured messages — e.g. two rising
  notes for Friday sunset, one high beep for market open, two low beeps for
  market close.
- **Louder option (later, no software change):** swap in a magnetic passive
  buzzer driven low-side by an S8050/2N3904 NPN — base resistor plus a 10k
  base pull-down so the boot constraint stays honest — with a flyback diode
  across the coil.

**Firmware note:** ESP8266 `tone()` uses the timer1 waveform generator.
Nothing in this codebase uses timer1 (TM1637 is bit-banged, display
brightness is chip-internal, no `analogWrite`), so there is no conflict.
`tone()` is non-blocking.

## Software design

Follows the existing module conventions (`button.h/cpp` is the template).

### New module: `beeper.h/cpp`

```cpp
enum class BeepPattern : uint8_t {
  kFridaySunset,   // two rising notes
  kTradingOpen,    // one high beep
  kTradingClose,   // two low beeps
};

void beeperBegin();                 // pinMode + ensure silent
void beeperTick(uint32_t nowMs);    // advances the active pattern; no-op when idle
void beeperPlay(BeepPattern p);     // starts a pattern (replaces any active one)
```

Internally a small non-blocking state machine: a pattern is a short table of
`{frequencyHz, durationMs}` steps (0 Hz = rest); `beeperTick()` walks the
table with `tone()` / `noTone()` against `millis()` deadlines. **No
`delay()` anywhere** — the main loop must keep servicing SQW, the display,
and web clients (the web server's `max loop gap` diagnostic exists precisely
to catch stalls).

### Hook points

Call `beeperPlay()` inside the **live-crossing branches** of
`friday_mode.cpp` and `trading_mode.cpp` — the same branches that call the
display manager's blinking `showInfo()` with the boundary message:

- Friday mode, `kToFridaySunset → kToSaturdaySunset` live crossing →
  `beeperPlay(BeepPattern::kFridaySunset)`.
- Trading mode, live open crossing → `kTradingOpen`; live close crossing →
  `kTradingClose`.

Placing the beep beside the message means it **inherits the `kNone` guard
for free**: crossings arriving from phase `kNone` (boot, config reload,
browser time sync) never announce, so they never beep.

Deliberately **not** hooked into `DisplayManager`'s blinking-overlay path:
`POST /api/message/test` previews go through that path and would beep on
every web preview.

### Main loop

`beeperTick(nowMs)` slots into `ClockApplication::tick()` alongside
`buttonTick()`.

## Follow-ups worth considering (not in v1)

- A `sounds.enabled` flag in `ClockConfig` (serialized via
  `config_serializer.cpp`, checkbox on `/settings`) — a market-open beep is
  charming until it isn't.
- Let `POST /api/message/test` accept `"beep": true` so patterns can be
  auditioned from the browser, like message previews.
- Optional per-boundary enable/pattern selection if one global flag proves
  too coarse.

## Shopping list

- 1 × passive piezo buzzer (12 mm disc, "passive"; KY-006 module is fine)
- 1 × ~220 Ω resistor

Wired: `D8 → resistor → piezo → GND`.
