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
- In compound `if` conditions, parenthesize each comparison explicitly —
  `if ((a == b) && (c < d))` — never rely on operator precedence.
- Every class/struct declaration is preceded by a comment stating what it is
  responsible for and how it fulfills that responsibility.
- Every member variable carries a brief same-line comment describing its role.

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

# Edit web pages against a live device without reflashing: serves web/
# sources raw at localhost:8080 and proxies /api/* to the device
python tools/dev_server.py --device <clock-ip>

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
- Each line is prefixed with `logCurrentTime()`, the peak cont-stack usage in bytes (bare value, of 4096), and `logSourceName(__FILE__):__LINE__`.
- Both macros keep their strings in **flash** (`PSTR` + `Serial.printf_P`). On the ESP8266 a plain string literal occupies RAM for the life of the program; moving the log strings to flash is what holds static RAM under 50% (OTA headroom). Consequences:
  - `LOG_PRINTLN(msg)` and the `fmt` of `LOG_PRINTF` **must be string literals**. For a runtime string, use `LOG_PRINTF("%s", value)`.
  - `LOG_PRINTLN` pastes its literal into the printf format, so a literal `%` must be written `%%`.
- Each macro call emits exactly one line and appends the terminating newline itself, so **formats must not end with `\n`**.

### Display / mode architecture
The display system has four layers:

1. **`display_format.h/cpp`** - the clock/counting format catalog and pure renderers (no I/O).
   - `config.h` owns the persisted `Mode` enum. `display_format.h` owns `FormatGroup`, `DisplayFormatInfo`, `displayFormatCount()`, `displayFormatInfo()`, `renderCountingFormat()`, and `renderClockFormat()`.
   - The **single source of truth** is in `display_format.cpp`: each `FormatSpec` pairs its UI label, `RefreshRate`, and `ColonAnimation` with three direct panel-renderer functions and an optional explicit overflow fallback. Keep the scheduling metadata consistent with those renderers.
   - Countdown and CountUp share `kCountingFormats`; the two modes cannot drift apart.
   - `renderCountingFormat()` and `renderClockFormat()` return a complete `DisplayFrame`. Each catalog panel calls a small reusable `PanelRenderer` directly; there is no enum/switch interpreter.
   - Label tokens: counting uses `dd`/`hh`/`mm`/`ss`/`u` (tenths) and `hhh` (total hours = days*24+hours); clock uses `YYYY`/`MM`/`DD`/`DOW`/`hh`/`mm`/`ss`/`u`. `H` and `N` are labels rendered as lowercase `h`/`n`; `DOW` renders Sun/non/tu/uEd/thu/Fri/Sat (7-segment-safe forms).
   - A semicolon in a clock label (`hh;mm`) marks a blinking colon; its `FormatSpec` uses `renderBlinkingHourMinute` and `ColonAnimation::kBlinking`. Fixed or absent colons use `ColonAnimation::kNone` because they require no animation scheduling.
   - `hhh:mm` combined on one panel only works through 99:59; above 99 hours its `FormatSpec::overflowFallback` explicitly selects the matching split `hhh | mm` variant.
   - Numeric-only panels are right-justified across the four characters (`7` renders as `"   7"`). For colon formats, the value left of the colon is blank-padded, not zero-padded (` 9:05`). When a blinking colon is off, the time renders without a separator (` 905`) so all digits remain visible.

2. **`display.h/cpp`** - `ClockApplication` owns `SegmentDisplay`, which wraps 3 `TM1637Display` objects and is attached to `DisplayManager` during startup.
   - `begin(brightness)`, `setBrightness(0-7)`, `showPanels(r1, r2, r3)`, `blank()`.
   - Panel strings use `:` or `;` between the second and third visible slots as non-consuming markup for the panel's center colon. This hardware has no decimal points; `.` has no special rendering behavior.
   - Caches last-written segments per panel; skips hardware write on identical content.
   - ASCII-to-segment glyph mapping lives in `display.cpp` as `ASCII_SEGMENTS`; adjust that table when a letter does not display well on 7-segment hardware.

3. **`display_manager.h/cpp`** - application-owned `DisplayManager`; the single entry point for all display state.
   - The model: the persisted **Mode** resolves to a base **View** (`View::kClock/kCountdown/kCountup` - what content is currently rendered), optionally covered by a temporary **Overlay** (`Overlay::kDemo/kMessage/kPagedMessage`).
   - `ViewState` is a plain struct: `{view, anchor, formatIndex, longFormatIndex}`. `anchor` is the countdown end time or countup start time; unused for clock. No unions. `longFormatIndex` (default `kSameFormat` = disabled) selects an alternate counting format while the remaining/elapsed duration is >= 24h; `activeCountingFormatIndex()` resolves it fresh on every render (and for the refresh cadence), so the display reverts to `formatIndex` on its own when the duration drops below 24h - no crossing state is kept.
   - `OverlayState` is a plain struct: `{overlay, blink, chainFinalMessage, message[64], paged, transition}`. `chainFinalMessage` makes an expiring overlay chain into the blinking final message (the demo's second phase) instead of restoring the base view.
   - `applySettings(config)` (hot-reload, no reboot), `tick(nowMs)`, and `setBrightness()`.
   - `setView(state)` replaces the base view. If an overlay is active, the new view simply becomes visible when the overlay clears - the view keeps updating live underneath; there is no snapshot to keep in sync. Used by `FridayModeController` to switch phases.
   - Overlays: `showSplash(msg)`, `showDemo()`, `showInfo(msg, durationMs = FOREVER)`, `showPages(pages, count, ...)`, `clearOverlay()`.
   - `activeMode()` is the persisted mode; `activeView()` is the base view (never an overlay). Friday and Trading modes update their views over time.
   - Clock colon blink toggles once per second (2-second full cycle); message/page blinking uses its own 500ms cadence. `DisplayFormatInfo::refreshRate` selects 100ms for tenths formats and 1s for the others.
   - Tenths values come from the injected RTC service's `msIntoSecond(nowMs)`, not `millis() % 1000`. `notifySecondBoundary()` invalidates the render throttle on each accepted SQW pulse. Demo tenths remain deadline-derived.
   - When `ClockConfig.display.clockUse12Hour` is true, hours are converted to the 1-12 scale locally in the clock renderer only; countdown/countup are unaffected.

4. **`clock_controller.h/cpp`** - coordinates application actions. It applies configuration to the owned `DisplayManager` and Friday mode, handles second boundaries, and exposes display previews to web APIs.
5. **`time_api.h/cpp`** - owns `GET /api/time` and `POST /api/time`; reads through `RtcService` and synchronizes through `ClockController`.

### Friday Mode
 - This needs an accurate sunset calcualtor. I use https://github.com/jpb10/SolarCalculator.git calculator. NASA's calculator is at https://github.com/jpb10/SolarCalculator.git
 - **`sunset_calculator.h/cpp`**: `calculateSunset(localDate, location)` - uses SolarCalculator to return a `DateTime` for local sunset given a `Location` (lat/lon/UTC offset).
  - SolarCalculator returns UTC hours. The code derives the UTC calculation date from the requested local sunset date by anchoring at 18:00 local, converts the returned UTC sunset to a UTC `DateTime`, then applies `utcOffsetMinutes` once to return local time.
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

### Trading Mode
- **`trading_mode.h/cpp`** owns the weekday 09:30-open / 16:00-close schedule and uses the RTC value as Eastern local wall-clock time.
- Trading mode always installs `View::kCountdown` through `DisplayManager::setView()`; it reuses the counting format catalog and never adds a separate view or renderer.
- `trading.formatOver24` (persisted; `kSameFormat`/255 = disabled) is passed to `ViewState.longFormatIndex` so weekend/overnight countdowns over 24h can render with a days-bearing format while the regular `trading.format` takes over below 24h.
- Before 09:30 on a weekday it counts down to that opening; during trading hours it counts down to 16:00; after close and on weekends it counts down to the next weekday opening. Holidays and early closes are not modeled.
- Boundary announcements follow the Friday-mode pattern: a live open-to-close phase crossing first installs the 16:00 countdown, then blinks `messages.tradingOpen` for 5s; a live close-to-open crossing installs the next opening countdown, then blinks `messages.tradingClose` for 5s.
- Boot, config reload, and browser time synchronization reset the remembered Trading phase to `kNone`, and a crossing from `kNone` never announces - so those events cannot synthesize an open/close message.

### Input
- **`button.h/cpp`**: `buttonBegin()`, `buttonTick()` (debounce), `buttonHasEvent()`, `buttonNextEvent()`.
  - `ButtonEvent` enum: `SHOW_SSID`, `SHOW_IP_ADDRESS`, `SHOW_RTC_STATUS`.
- **`page_manager.h/cpp`**: `ClockApplication` owns `PageManager` and injects its `DisplayManager` dependency. `showSsid(ssid)` and `showIpAddress(ip)` build `DisplayPage` arrays and hand them to `showPages()`.

### Geography
- **`zipcode.h/cpp`**: `zipcodeLookupLocation(zipcode, &out, path)` - searches `/zipcodes.txt` on LittleFS; `isValidZipcode(zipcode)`. This table is not very accurate unfortunatly, but close enough.
  - The `dst` flag is persisted and echoed by APIs, but sunset math uses only numeric `utcOffsetMinutes`.

### Storage / config
- `ClockConfig` (in `config.h`) holds: `activeMode`; display, counting, Friday, Trading, message, and location groups; `timezone` with its IANA name and numeric UTC offset; and the persisted `dst` flag.
- `display.clockUse12Hour` serializes as `display.clock12Hour` (boolean) in `/config.json`. Default `false` (24-hour).
- `ClockConfig.messages` stores `splash`, `final`, `fridaySunset`, `tradingOpen`, and `tradingClose`; they serialize under `display.messages` and are sanitized with `sanitizeDisplayMessage` (max 12 printable ASCII characters). Trading boundary defaults are `"OPEN"` and `"CLOSE"`.
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
  - `WebPortal` (internal) owns the `ESP8266WebServer`, `HttpResponder`, and the domain API handlers (`ConfigApi`, `TimeApi`, `LocationApi`, `FileApi`, `WifiApi`). Endpoint domains remain separate where they contain meaningful behavior.
  - **Every page is a static gzipped PROGMEM asset; all dynamic data flows through the JSON APIs.** Page sources live in `web/` (`pages/*.html`, `common.css`, `common.js`); `tools/build_web.py` (a PlatformIO pre-script) gzips them into a `kWebAssets` table that `WebPortal::begin()` registers in one loop. Never build HTML on the server. The route → file mapping lives only in `tools/web_manifest.py`, shared by the build and by `tools/dev_server.py` (edit-reload page development against a live device, no reflash).
  - `web/common.js` owns the shared page helpers (`$`, `api`/`apiPost`, `setStatus`, error/slow-load beacons to `POST /api/client-log`, `reportFieldMismatch`, `setFieldFromConfig`); `web/common.css` is the single stylesheet. Both are served hash-versioned (`?v=`) with an immutable cache header, so each page transfers only its own small body; pages stay `no-cache`.
  - Runs `DNSServer` for captive portal only when in AP mode.
  - UI pages: `GET /`, `/settings`, `/files`, `/format`, `/time`, `/sunset`, `/messages`, `/location`, `/wifi`, `/view`.
  - REST API: `GET /api/status` (device name, configured mode, and live demo state for the home page), `POST /api/config`, `GET /api/config`, `GET /api/formats`, `POST /api/mode`, `POST /api/brightness`, `GET|POST /api/time`, `POST /api/sunset`, `GET /api/zipcode/lookup`, `POST /api/demo/test`, `POST /api/message/test`, `POST /api/field-mismatch`, `GET /api/wifi/status`, `GET /api/wifi/scan`, `POST /api/wifi/connect`.
  - File management: `GET /api/files`, `GET|DELETE /api/file`, `POST /api/file/upload`.
  - AP-mode radio settings in `wifi_connection_manager.cpp` (11g phy mode, channel survey, 17 dBm TX) are evidence-backed fixes for transfer stalls with power-save phone clients; code comments record what was observed, including two settings that were tried and made things worse. Do not change them without new on-device evidence.
  - **Never judge WiFi/page-load performance while the clock is USB-powered from the PC** (confirmed 2026-07-14): on the PC's USB port next to its 2.4 GHz Bluetooth radio, AP transfers to the phone degrade severely (truncated bodies, ~18 s for a 1.4 KB page) with identical firmware, healthy heap, and fast handlers; on its own power supply away from the PC, loads are fast. This matches the documented USB supply-droop behavior (TX spikes corrupt long frames; that is why TX power is 17 dBm). To separate device work from radio delivery: `time=` in the request log is device-side cost; the browser beacon's `dl=` is delivery time; `TRUNCATED wrote X of Y` means the client stopped ACKing mid-transfer.
- `POST /api/message/test` accepts an optional `"blink": true`, which previews via the blinking `showInfo(msg, 5000)` path instead of the static `showSplash()` - used by Friday and Trading boundary-message previews so they match live behavior.
- `/location` edits the persisted device `location`. `/sunset` edits/persists only the `sunset` test fields before posting to `/api/sunset`.
- On `/time`, "Set Time from Browser" updates the RTC/config (and resets the Friday sunset cache) and mirrors the new browser-derived values into the Device fields after a successful save.
