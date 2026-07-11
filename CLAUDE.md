# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

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
- Follow Google coding conventions when applicable: `PascalCase` types,
  `camelCase` functions and locals, trailing underscores for private members,
  and `kPascalCase` constants and enum values. Reserve `ALL_CAPS` for macros
  and established hardware pin identifiers.

## ARCHITECTURE
- Separate concerns into distinct layers (data, logic, presentation).
- Define clear boundaries between modules.
- Avoid leaky abstractions.
- Prefer explicit behavior over implicit behavior.

## OUTPUT FORMAT
- Before writing code, briefly explain design decisions and tradeoffs.
- After code changes, note further improvements worth considering.
- If the task is large, outline the structure first and confirm before implementing.

## BUILD COMMANDS

```bash
# Build firmware
pio run

# Build and upload to device
pio run --target upload

# Upload filesystem (LittleFS data/ directory) to device
pio run --target uploadfs

# Monitor serial output (74880 baud with ESP8266 exception decoder)
pio device monitor

# Clean build artifacts
pio run --target clean
```

There are no automated tests. Validation is done by flashing the firmware and observing behavior on device. Serial output at 74880 baud includes stack traces decoded by `monitor_filters = esp8266_exception_decoder`.

## ESP8266 CLOCK PROJECT CONVENTIONS

### Hardware pin map (authoritative source: `src/hardware.h`)

| Signal        | Pin | GPIO   | Notes                                                              |
|---------------|-----|--------|--------------------------------------------------------------------|
| *Left side*   |     |        |                                                                    |
| TM1637 DIO[2] | D0  | GPIO16 | No interrupts; fine for DIO                                        |
| TM1637 CLK    | D5  | GPIO14 | Shared across all 3 displays                                       |
| TM1637 DIO[1] | D6  | GPIO12 | Safe                                                               |
| DS3231 SQW    | D7  | GPIO13 | RISING interrupt, INPUT_PULLUP, 1Hz                                |
| D8 (unused)   | —   | GPIO15 | **Must stay LOW at boot (strapping pin); do not connect**          |
| *Right side*  |     |        |                                                                    |
| DS3231 SCL    | D1  | GPIO5  | Hardware I2C                                                       |
| DS3231 SDA    | D2  | GPIO4  | Hardware I2C                                                       |
| Button        | D3  | GPIO0  | INPUT_PULLUP, pressed = LOW; do not hold at boot                   |
| TM1637 DIO[0] | D4  | GPIO2  | Shares with INTERNAL_LED; both idle HIGH                           |
| Internal LED  | D4  | GPIO2  | Active-low; shared with TM1637 DIO[0] — LED flickers during write |

### Pin boot constraints
- **GPIO15 (D8)**: must be LOW - leave unconnected; any pull-up prevents boot/flash.
- **GPIO0 (D3)**: must be HIGH - INPUT_PULLUP + button not pressed.
- **GPIO2 (D4)**: must be HIGH - LED and TM1637 DIO both idle HIGH; safe.

### Serial / I2C / RTC time
- Serial at 74880 baud for readable ESP8266 boot output.
- Initialize I2C early in `setup()` with explicit SDA/SCL pins before probing the RTC.
- `ClockApplication` owns `RtcService` from `rtc_ds3231.h` and injects it into the controller, display manager, and config API. The module keeps its ISR bridge and hardware state private.
  - `begin()`, `getStatus()`, `getNow()` (live I2C read), and `setNow()` (also resyncs the cache) provide device operations.
  - SQW processing uses `beginSqwProcessing()` and `consumeSqwPulse()`. Gate time-sensitive per-second logic on the latter.
  - `isLogIntervalDue()` is only for pacing the periodic log line and cache resync; never gate transitions on it.
  - `getNowCached()` provides second-resolution time at zero I2C cost and is required on the display-render path.
  - `rtcIsHealthy()`: RTC present and SQW pulse arriving on schedule. Drives the "no rtc" overlay in `main.cpp`.
  - `msIntoSecond(nowMs)` is clamped to 0-999 and phase-locked to the ISR timestamp. Never compute tenths from `millis() % 1000` directly.
- For fatal exception debugging, keep exception decoding enabled and include decoded stack traces in reports.

### Logging
- `log.h` provides `LOG_PRINTLN(msg)` and `LOG_PRINTF(fmt, ...)` macros.
- Each line is prefixed with `logCurrentTime()` and `logSourceName(__FILE__):__LINE__`.
- Both macros keep their strings in **flash** (`PSTR` + `Serial.printf_P`). On the ESP8266 a plain string literal occupies RAM for the life of the program; moving the log strings to flash is what holds static RAM under 50% (OTA headroom). Consequences:
  - `LOG_PRINTLN(msg)` and the `fmt` of `LOG_PRINTF` **must be string literals**. For a runtime string, use `LOG_PRINTF("%s\n", value)`.
  - `LOG_PRINTLN` pastes its literal into the printf format, so a literal `%` must be written `%%`.

### Display / mode architecture
The display system has four layers:

1. **`format.h` + `clock_format.h/cpp`** - the format catalog and the pure renderers (no I/O).
   - `format.h` declares: `Mode` enum (`kModeCountdown/Countup/Clock/Friday` - the persisted mode), `FormatGroup` enum, and the catalog accessors `formatCount(group)`, `getFormat(group, index)` (UI label string), `formatHasTenths(group, index)`, `clockFormatBlinksColon(index)`, `resolveCountingOverflowIndex(index, totalHours)`.
   - The **single source of truth** is in `clock_format.cpp`: the `kCountingFormats[]` and `kClockFormats[]` tables. Each entry pairs the UI label with the three per-row render ops, so adding or reordering a format edits **exactly one table row**. There are no parallel tables and no stored metadata: `hasTenths` and `blinkColon` are *derived* from the row ops - never hardcode index lists or add metadata flags.
   - Countdown and CountUp share `kCountingFormats`; the two modes cannot drift apart.
   - `renderCountdown/renderCountup/renderClock(idx, fields, r1, r2, r3, ...)` each fill three 4-char row buffers, one per physical display. Row ops dispatch to fragment helpers (`fmtNumber`, `fmtColonAnchored`, `fmtDaysWithLabel`, ...) - reuse these; do not add bare `snprintf` duplicates.
   - Label tokens: counting uses `dd`/`hh`/`mm`/`ss`/`u` (tenths) and `hhh` (total hours = days*24+hours); clock uses `YYYY`/`MM`/`DD`/`DOW`/`hh`/`mm`/`ss`/`u`. `H` and `N` are labels rendered as lowercase `h`/`n`; `DOW` renders Sun/non/tu/uEd/thu/Fri/Sat (7-segment-safe forms).
   - A semicolon in a clock label (`hh;mm`) marks a blinking colon (the `kHourMinBlink` op).
   - `hhh:mm` combined on one row only works through 99:59; above 99 hours rendering auto-falls back to a compatible split `hhh | mm` variant via `resolveCountingOverflowIndex()` (semantic, based on row ops - no hardcoded indices).
   - Numeric-only rows are right-justified across the 4-character panel (`7` renders as `"   7"`). For colon formats, the value left of the colon is blank-padded, not zero-padded (` 9:05`). When a blinking colon is off, the time renders without a separator (` 905`) so all digits remain visible.

2. **`display.h/cpp`** - `ClockApplication` owns `SegmentDisplay`, which wraps 3 `TM1637Display` objects and is attached to `DisplayManager` during startup.
   - `begin(brightness)`, `setBrightness(0-7)`, `showPanels(r1, r2, r3)`, `blank()`.
   - Panel strings are 4 chars; `:` or `;` between positions 1-2 lights the colon; `.` lights the decimal on the preceding digit.
   - Caches last-written segments per panel; skips hardware write on identical content.
   - ASCII-to-segment glyph mapping lives in `display.cpp` as `ASCII_SEGMENTS`; adjust that table when a letter does not display well on 7-segment hardware.

3. **`display_manager.h/cpp`** - application-owned `DisplayManager`; the single entry point for all display state.
   - The model: the persisted **Mode** resolves to a base **View** (`View::kClock/kCountdown/kCountup` - what content is currently rendered), optionally covered by a temporary **Overlay** (`Overlay::kDemo/kMessage/kPagedMessage`).
   - `ViewState` is a plain struct: `{view, anchor, formatIndex}`. `anchor` is the countdown end time or countup start time; unused for clock. No unions.
   - `OverlayState` is a plain struct: `{overlay, blink, chainFinalMessage, message[64], paged, transition}`. `chainFinalMessage` makes an expiring overlay chain into the blinking final message (the demo's second phase) instead of restoring the base view.
   - `applySettings(config)` (hot-reload, no reboot), `tick(nowMs)`, and `setBrightness()`.
   - `setView(state)` replaces the base view. If an overlay is active, the new view simply becomes visible when the overlay clears - the view keeps updating live underneath; there is no snapshot to keep in sync. Used by `FridayModeController` to switch phases.
   - Overlays: `showSplash(msg)`, `showDemo()`, `showInfo(msg, durationMs = FOREVER)`, `showPages(pages, count, ...)`, `clearOverlay()`.
   - `activeMode()` is the persisted mode; `activeView()` is the base view (never an overlay). Friday is the only mode whose view changes over time.
   - Clock colon blink toggles once per second (2-second full cycle); message/page blinking uses its own 500ms cadence. Tenths formats refresh at 100ms, others at 1s (`formatHasTenths` drives this).
   - Tenths values come from the injected RTC service's `msIntoSecond(nowMs)`, not `millis() % 1000`. `notifySecondBoundary()` invalidates the render throttle on each accepted SQW pulse. Demo tenths remain deadline-derived.
   - When `ClockConfig.display.clockUse12Hour` is true, hours are converted to the 1-12 scale locally in the clock renderer only; countdown/countup are unaffected.

4. **`clock_controller.h/cpp`** - coordinates application actions. It applies configuration to the owned `DisplayManager` and Friday mode, handles second boundaries, and exposes display previews to web APIs.
5. **`time_api.h/cpp`** - owns `GET /api/time` and `POST /api/time`; reads through `RtcService` and synchronizes through `ClockController`.

### Friday Mode
- **`friday_mode.h/cpp`**: `FridayModeController` (internal singleton). Public API: `fridayModeApplySettings(config)`, `fridayModeTick(now)`, `fridayModeResetSunsetCache()`.
- `fridayModeTick()` is called once per SQW second from `main.cpp`; self-gates - does nothing unless `activeMode == kModeFriday`, and short-circuits when the phase hasn't changed.
- Phase logic (all times local, derived from `locations.device` + `timezone.utcOffsetMinutes`):
  - **Clock phase**: Saturday sunset through Thursday midnight; also the default.
  - **To Friday sunset**: Thursday midnight → Friday sunset.
  - **To Saturday sunset**: Friday sunset → Saturday sunset.
- Each phase transition receives the application-owned `DisplayManager` explicitly and calls `setView()` with a `ViewState` built from `ClockConfig.friday`.
- Crossing Friday sunset **live** (previous phase `kToFridaySunset` → `kToSaturdaySunset` while running) blinks `ClockConfig.messages.fridaySunset` for 5s via the supplied display manager, after the Saturday-sunset countdown becomes the base view. The previous-phase check is deliberate: arriving at `kToSaturdaySunset` from `kNone` must **not** fire the message.
- Sunset targets are cached and recomputed at most once per week (when `fridayDateFor(now)` changes). `calculateSunset()` is **not** called on every tick.
- `applySettings()` and `fridayModeResetSunsetCache()` (called after a browser time sync) invalidate the cache to force recomputation on the next tick.

### Input
- **`button.h/cpp`**: `buttonBegin()`, `buttonTick()` (debounce), `buttonHasEvent()`, `buttonNextEvent()`.
  - `ButtonEvent` enum: `SHOW_SSID`, `SHOW_IP_ADDRESS`, `SHOW_RTC_STATUS`.
- **`page_manager.h/cpp`**: `ClockApplication` owns `PageManager` and injects its `DisplayManager` dependency. `showSsid(ssid)` and `showIpAddress(ip)` build `DisplayPage` arrays and hand them to `showPages()`.

### Geography
- **`zipcode.h/cpp`**: `zipcodeLookupLocation(zipcode, &out, path)` - searches `/zipcodes.txt` on LittleFS; `isValidZipcode(zipcode)`.
- **`sunset_calculator.h/cpp`**: `calculateSunset(localDate, location)` - uses SolarCalculator to return a `DateTime` for local sunset given a `Location` (lat/lon/UTC offset).
  - SolarCalculator returns UTC hours. The code derives the UTC calculation date from the requested local sunset date by anchoring at 18:00 local, converts the returned UTC sunset to a UTC `DateTime`, then applies `utcOffsetMinutes` once to return local time.
  - The `dst` flag is persisted and echoed by APIs, but sunset math uses only numeric `utcOffsetMinutes`.

### Storage / config
- `ClockConfig` (in `config.h`) holds: `activeMode`; display, counting, Friday, message, and location groups; `timezone` with its IANA name and numeric UTC offset; and the persisted `dst` flag.
- `display.clockUse12Hour` serializes as `display.clock12Hour` (boolean) in `/config.json`. Default `false` (24-hour).
- `ClockConfig.messages` stores `splash`, `final`, and `fridaySunset`; they serialize under the unchanged `display.messages` JSON object and are sanitized with `sanitizeDisplayMessage` (max 12 printable ASCII characters). Defaults live in `defaults.cpp` (`fridaySunset` defaults to `"     SUN SET"`).
- `LocationInfo` contains `latitude`, `longitude`, and `zipcode[6]`. `ClockConfig.locations` keeps distinct `device` and `sunsetTest` values. `/config.json` retains separate `location` and `sunset` objects. Do not cross-read one for the other or use one as a fallback for the other.
- `WifiConfig` holds: `staSsid`, `staPassword`, `apSsid`, `apPassword`.
- **`config_serializer.h/cpp` is the only home of the JSON schema, in both directions.** Never spell out config field paths anywhere else.
  - Struct → JSON: `serializeClockConfig(doc, config)`, `serializeWifiConfig(doc, wifi)` (full, for disk), `serializeWifiStatus(doc, wifi)` (no station password, for HTTP responses).
  - JSON → struct: `applyJsonToClockConfig(root, cfg)` and `applyJsonToWifiConfig(root, wifi)` use **patch semantics** (absent fields untouched). Loading `/config.json` (base = defaults) and applying a `POST /api/config` payload (base = loaded config) are the same operation through the same function. `applyJsonToClockConfig` returns `nullptr` or a static error-JSON for the first invalid value; it keeps applying the remaining fields so one bad value can't wipe the rest of the file on load, and API callers discard the partial cfg on error.
- `ClockApplication` owns `ConfigManager` and `WifiConnectionManager` and injects them into the web APIs. Configuration saves read the existing `/config.json`, patch their section, and atomically replace the file (tmp + rename), so the other section is preserved.
- **`config_validation.h/cpp`** - sanitizers and conversions. Canonical home of `modeName(mode)` / `modeFromName(name, out)` / `sanitizeMode`, `sanitizeFormatIndex`, `sanitizeBrightness`, `sanitizeUtcOffsetMinutes`, `sanitizePrintableText`, `sanitizeDisplayMessage`. Do not redeclare these elsewhere.

### Networking
- **`wifi_connection_manager.h/cpp`**: application-owned `WifiConnectionManager`, injected into the web portal and `WifiApi`.
  - `begin(config)`: tries STA first (if `staSsid` is set, 15s timeout); falls back to AP.
  - `tick()`: handles deferred events (e.g. AP client connected logging).
  - `status()` returns `WifiRuntimeStatus` (mode, connected, ssid, ip, apSsid, apIp).
  - `scanNetworks(doc)`, `connectAndSave(ssid, password)` for web-driven network switching.
- **`web_server.h/cpp`**: `webBegin()`, `webHandleClients()` (must be called every loop), `networkGetInfo(ssid, ip)`, `webScheduleReboot(delayMs)` (deferred reboot, gives the HTTP response time to flush).
  - `WebPortal` (internal) owns the `ESP8266WebServer`, `HttpResponder`, HTML page routes, and the domain API handlers (`ConfigApi`, `TimeApi`, `LocationApi`, `FileApi`, `WifiApi`). Simple page serving stays in `web_server.cpp`; endpoint domains remain separate where they contain meaningful behavior.
  - Runs `DNSServer` for captive portal only when in AP mode.
  - UI pages: `GET /`, `/settings`, `/config`, `/format`, `/time`, `/sunset`, `/messages`, `/location`, `/wifi`.
  - REST API: `POST /api/config`, `GET /api/config`, `GET /api/formats`, `POST /api/mode`, `POST /api/brightness`, `GET|POST /api/time`, `POST /api/sunset`, `GET /api/zipcode/lookup`, `POST /api/demo/test`, `POST /api/message/test`, `POST /api/field-mismatch`, `GET /api/wifi/status`, `GET /api/wifi/scan`, `POST /api/wifi/connect`.
  - File management: `GET /api/files`, `GET|DELETE /api/file`, `POST /api/file/upload`, `GET /view`.
- `POST /api/message/test` accepts an optional `"blink": true`, which previews via the blinking `showInfo(msg, 5000)` path instead of the static `showSplash()` - used by the `/messages` page's "Test Friday Sunset" button so the preview matches the real sunset behavior.
- `/location` edits the persisted device `location`. `/sunset` edits/persists only the `sunset` test fields before posting to `/api/sunset`.
- On `/time`, "Set Time from Browser" updates the RTC/config (and resets the Friday sunset cache) and mirrors the new browser-derived values into the Device fields after a successful save.
