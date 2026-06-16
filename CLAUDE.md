# CLAUDE.md

You are a senior software engineer with 15+ years of experience. When providing code solutions, follow these principles:

## DESIGN PRINCIPLES
- Apply SOLID principles strictly (Single Responsibility, Open/Closed, Liskov, Interface Segregation, Dependency Inversion).
- Minimize coupling between classes/modules. Prefer dependency injection over hard dependencies.
- Favor composition over inheritance.
- Use clear abstractions and interfaces to separate concerns.
- Within a file, prefer classes (including singletons). Across files, prefer functions for module boundaries.

## CODE READABILITY
- Write self-documenting code with meaningful names that reveal intent.
- Keep functions small and focused (do one thing).
- Avoid deep nesting. Use early returns and guard clauses.
- Add concise comments only where the "why" is not obvious.
- Follow Google coding conventions when applicable.

## ARCHITECTURE
- Separate concerns into distinct layers (data, logic, presentation).
- Define clear boundaries between modules.
- Avoid leaky abstractions.
- Prefer explicit behavior over implicit behavior.

## ESP8266 CLOCK PROJECT CONVENTIONS

### Hardware pin map
| Signal | Pin | GPIO | Notes |
|---|---|---|---|
| TM1637 CLK (shared) | D5 | GPIO14 | All 3 displays |
| TM1637 #1 DIO | D0 | GPIO16 | `SEGMENT_DIO[0]` |
| TM1637 #2 DIO | D4 | GPIO2  | `SEGMENT_DIO[1]` — shares with INTERNAL_LED; both idle HIGH |
| TM1637 #3 DIO | D6 | GPIO12 | `SEGMENT_DIO[2]` |
| DS3231 SDA | D2 | GPIO4 | Hardware I2C |
| DS3231 SCL | D1 | GPIO5 | Hardware I2C |
| DS3231 SQW | D7 | GPIO13 | RISING interrupt, INPUT_PULLUP, 1Hz |
| Button | D3 | GPIO0 | INPUT_PULLUP, pressed = LOW; do not hold at boot |
| Internal LED | D4 | GPIO2 | Active-low; shared with TM1637 #2 DIO — LED flickers during transmit |
| D8 | — | GPIO15 | **Unused — must stay LOW at boot (strapping pin); do not connect** |

### Pin boot constraints
- **GPIO15 (D8)**: must be LOW — leave unconnected; any pull-up prevents boot/flash.
- **GPIO0 (D3)**: must be HIGH — INPUT_PULLUP + button not pressed.
- **GPIO2 (D4)**: must be HIGH — LED and TM1637 DIO both idle HIGH; safe.

### Serial / I2C / RTC
- Serial at 74880 baud for readable ESP8266 boot output.
- Initialize I2C early in `setup()` with explicit SDA/SCL pins before probing the RTC.
- RTC SQW runs at 1Hz RISING interrupt; `rtcSqwPendingPulseCount` is consumed in loop by both the SQW logger and the mode engine.
- `rtcGetNow()` returns a `DateTime` from RTClib; returns `DateTime(2000,1,1)` when RTC is absent.
- For fatal exception debugging, keep exception decoding enabled and include decoded stack traces in reports.

### Display / mode architecture
The display system has three layers:

1. **`format.h/cpp`** — static format-string tables and `ClockSettings` struct.
   - `DisplayMode` enum: `kModeCountdown`, `kModeCountup`, `kModeClock`, `kModeDemo`, `kModeInfo`.
   - `ClockSettings` holds: `activeMode`, format indices, `countdownTarget`, `countupStart`, `infoMessage[64]`, `brightness`.
   - `defaultClockSettings()` returns sensible defaults.

2. **`clock_format.h/cpp`** — pure renderers (no I/O). Each fills three 8-byte string buffers (r1/r2/r3), one per physical display.
   - `renderCountdown(idx, fields, r1, r2, r3)`
   - `renderCountup(idx, fields, r1, r2, r3)`
   - `renderClock(idx, fields, r1, r2, r3, colonVisible)`
   - Helper predicates: `countdownHasTenths(idx)`, `clockBlinkColon(idx)`, etc.

3. **`display.h/cpp`** — `SegmentDisplay` singleton wrapping 3 `TM1637Display` objects.
   - `begin(brightness)`, `setBrightness(0–7)`, `showPanels(r1, r2, r3)`, `blank()`.
   - Maps ASCII chars (0–9, space, `-`, `D`, `H`, `N`, `u`, `.`) to 7-segment bitmasks via `setSegments()`.
   - A `:` or `;` at position 2 in a panel string lights the TM1637 colon (high bit on segment[1]).

4. **`clock_mode.h/cpp`** — `ClockModeEngine` singleton.
   - `begin(settings)`, `applySettings(settings)` (hot-reload, no reboot), `tick(millis())`.
   - Self-throttles: 100ms refresh when format has tenths; 1000ms otherwise; 500ms blink for demo/colon; 350ms scroll step for info.
   - Demo mode: 5-second countdown on display 3, then blinks `infoMessage` across all 3 at 500ms.
   - Info mode: static on centre display if ≤ 4 chars; scrolls across all 12 positions otherwise.

### Storage / networking
- Single `/config.json` on LittleFS holds both AP credentials and all `ClockSettings` fields.
- `ConfigManager::loadApConfig()` and `loadClockSettings()` / `saveClockSettings()` in `config.h/cpp`.
- `saveClockSettings()` reads existing file first to preserve AP fields before writing back.
- Web server runs as WiFi AP (captive portal + DNS wildcard).
  - `GET /` — status page (network, I2C scan, RTC); links to `/config`.
  - `GET /config` — renders `CONFIG_HTML` template with current settings injected.
  - `POST /config` — saves to flash, hot-reloads engine via `clockModeEngine.applySettings()`, redirects to `GET /config?saved=1`.
- `webHandleClients()` must be called every loop iteration.

## OUTPUT FORMAT
- Before writing code, briefly explain design decisions and tradeoffs.
- After code changes, note further improvements worth considering.
- If the task is large, outline the structure first and confirm before implementing.
