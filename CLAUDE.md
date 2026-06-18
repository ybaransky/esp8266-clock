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

## OUTPUT FORMAT
- Before writing code, briefly explain design decisions and tradeoffs.
- After code changes, note further improvements worth considering.
- If the task is large, outline the structure first and confirm before implementing.

## ESP8266 CLOCK PROJECT CONVENTIONS

### Hardware pin map
| Signal | Pin | GPIO | Notes |
|---|---|---|---|
| TM1637 CLK (shared) | D5 | GPIO14 | All 3 displays |
| TM1637 `SEGMENT_DIO[0]` | D6 | GPIO12 | Safe |
| TM1637 `SEGMENT_DIO[1]` | D4 | GPIO2  | Shares with INTERNAL_LED; both idle HIGH |
| TM1637 `SEGMENT_DIO[2]` | D0 | GPIO16 | No interrupts; fine for DIO |
| DS3231 SDA | D2 | GPIO4 | Hardware I2C |
| DS3231 SCL | D1 | GPIO5 | Hardware I2C |
| DS3231 SQW | D7 | GPIO13 | RISING interrupt, INPUT_PULLUP, 1Hz |
| Button | D3 | GPIO0 | INPUT_PULLUP, pressed = LOW; do not hold at boot |
| Internal LED | D4 | GPIO2 | Active-low; shared with TM1637 DIO[1] - LED flickers during transmit |
| D8 | - | GPIO15 | **Unused - must stay LOW at boot (strapping pin); do not connect** |

### Pin boot constraints
- **GPIO15 (D8)**: must be LOW - leave unconnected; any pull-up prevents boot/flash.
- **GPIO0 (D3)**: must be HIGH - INPUT_PULLUP + button not pressed.
- **GPIO2 (D4)**: must be HIGH - LED and TM1637 DIO both idle HIGH; safe.

### Serial / I2C / RTC
- Serial at 74880 baud for readable ESP8266 boot output.
- Initialize I2C early in `setup()` with explicit SDA/SCL pins before probing the RTC.
- RTC SQW runs at 1Hz RISING interrupt. Call `rtcBeginSqwProcessing()` after `rtcBegin()`, then `rtcProcessSqwPulse()` each loop - returns `true` on a log-interval pulse.
- `rtcGetNow()` returns a `DateTime` from RTClib; `rtcGetStatus()` returns `RtcStatus` (present, powerLost, lowBattery, sqwConfigured, error).
- For fatal exception debugging, keep exception decoding enabled and include decoded stack traces in reports.

### Logging
- `log.h` provides `LOG_PRINTLN(msg)` and `LOG_PRINTF(fmt, ...)` macros.
- Each line is prefixed with `logCurrentTime()` and `logSourceName(__FILE__):__LINE__`.

### Display / mode architecture
The display system has four layers:

1. **`format.h`** - format-group tables and mode enums.
   - `FormatGroup` enum: `kFmtGroupCountdown`, `kFmtGroupCountUp`, `kFmtGroupClock`.
   - `PersistentMode` enum: `kPersistentCountdown`, `kPersistentCountup`, `kPersistentClock` - the mode stored in config and restored after temporary states.
   - `getFormat(group, index)` and `formatCount(group)` are the public accessors.

2. **`clock_format.h/cpp`** - pure renderers (no I/O). Each fills three 4-char string buffers (r1/r2/r3), one per physical display.
   - `renderCountdown(idx, fields, r1, r2, r3)`
   - `renderCountup(idx, fields, r1, r2, r3)`
   - `renderClock(idx, fields, r1, r2, r3, colonVisible)`
   - Helper predicates: `countdownHasTenths(idx)`, `clockBlinkColon(idx)`, etc.

3. **`display.h/cpp`** - `SegmentDisplay` singleton wrapping 3 `TM1637Display` objects.
   - `begin(brightness)`, `setBrightness(0-7)`, `showPanels(r1, r2, r3)`, `blank()`.
   - Panel strings are 4 chars; `:` or `;` between positions 1-2 lights the colon; `.` lights the decimal on the preceding digit.
   - Caches last-written segments per panel; skips hardware write on identical content.

4. **`display_manager.h/cpp`** - `DisplayManager` singleton; the single entry point for all display state.
   - `begin(config)`, `applySettings(config)` (hot-reload, no reboot), `tick(nowMs)`.
   - Temporary display states (`showSplash`, `showInfo`, `showDemo`, `showPages`) overlay the persistent mode and expire or are cleared via `clearInfo()`.
   - `DisplayBehavior` enum: `kClock`, `kCountdown`, `kCountup`, `kMessage`, `kPagedMessage`.
   - State machine types: `DisplayState` (behavior + blink flag + `DisplayPayload`), `DisplayTransition` (expiration deadline), `PagedDisplayPayload` (up to 8 `DisplayPage` structs, each 3x4 chars).
   - `currentStateName()` returns the active behavior as a string (useful for SQW log lines).

5. **`clock_state.h`** - thin public API used by `web_server.cpp` to decouple it from `DisplayManager`.
   - `clockApplySettings(cfg)`, `clockSetBrightness(b)`, `clockTriggerDemo()`, `clockShowMessagePreview(msg)`, `clockShowInfo(msg, durationMs)`, `clockClearInfo()`.

### Input
- **`button.h/cpp`**: `buttonBegin()`, `buttonTick()` (debounce), `buttonHasEvent()`, `buttonNextEvent()`.
  - `ButtonEvent` enum: `SHOW_SSID`, `SHOW_IP_ADDRESS`, `SHOW_RTC_STATUS`.
- **`page_manager.h/cpp`**: `PageManager` singleton. `showSsid(ssid)`, `showIpAddress(ip)` - builds `DisplayPage` arrays and hands them to `displayManager.showPages()`.

### Geography
- **`zipcode.h/cpp`**: `zipcodeLookupLocation(zipcode, &out, path)` - searches `/zipcodes.txt` on LittleFS; `isValidZipcode(zipcode)`.
- **`sunset_calculator.h/cpp`**: `calculateSunset(localDate, location)` - returns a `DateTime` for local sunset given a `Location` (lat/lon).

### Storage / config
- `ClockConfig` (in `config.h`) holds: `activeMode`, format indices, `countdownDatetime[20]`, `countupDatetime[20]`, `splashMessage[64]`, `finalMessage[64]`, `brightness`, `latitude`, `longitude`, `zipcode[6]`, `timezone[40]`, `utcOffsetMinutes`.
- `WifiConfig` holds: `staSsid`, `staPassword`, `apSsid`, `apPassword`.
- `ConfigManager` singleton in `config.h/cpp`:
  - `loadClockConfig()` / `saveClockConfig(cfg)` - persisted to `/config.json` on LittleFS.
  - `loadWifiConfig()` / `saveWifiConfig(cfg)` - separate section of the same file.

### Networking
- **`wifi_connection_manager.h/cpp`**: `WifiConnectionManager` singleton.
  - `begin(config)`: tries STA first (if `staSsid` is set, 15s timeout); falls back to AP.
  - `tick()`: handles deferred events (e.g. AP client connected logging).
  - `status()` returns `WifiRuntimeStatus` (mode, connected, ssid, ip, apSsid, apIp).
  - `scanNetworks(doc)`, `connectAndSave(ssid, password)` for web-driven network switching.
- **`web_server.h/cpp`**: `webBegin()`, `webHandleClients()` (must be called every loop), `networkGetInfo(ssid, ip)`.
  - Runs `DNSServer` for captive portal only when in AP mode.
  - UI pages: `GET /`, `/settings`, `/config`, `/format`, `/time-sync`, `/messages`, `/location`, `/wifi`.
  - REST API: `POST /api/config`, `GET /api/config`, `GET /api/formats`, `POST /api/mode`, `POST /api/brightness`, `GET|POST /api/time/sync`, `GET /api/zipcode/lookup`, `POST /api/demo/test`, `POST /api/message/test`, `GET /api/wifi/status`, `GET /api/wifi/scan`, `POST /api/wifi/connect`.
  - File management: `GET /api/files`, `GET|DELETE /api/file`, `POST /api/file/upload`, `GET /view`.
- `webHandleClients()` must be called every loop iteration.
