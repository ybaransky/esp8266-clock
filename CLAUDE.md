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

### Hardware pin map

| Signal        | Pin | GPIO   | Notes                                                              |
|---------------|-----|--------|--------------------------------------------------------------------|
| *Left side*   |     |        |                                                                    |
| TM1637 DIO[2] | D0  | GPIO16 | No interrupts; fine for DIO                                        |
| TM1637 CLK    | D5  | GPIO14 | Shared across all 3 displays                                       |
| TM1637 DIO[0] | D6  | GPIO12 | Safe                                                               |
| DS3231 SQW    | D7  | GPIO13 | RISING interrupt, INPUT_PULLUP, 1Hz                                |
| D8 (unused)   | —   | GPIO15 | **Must stay LOW at boot (strapping pin); do not connect**          |
| *Right side*  |     |        |                                                                    |
| DS3231 SCL    | D1  | GPIO5  | Hardware I2C                                                       |
| DS3231 SDA    | D2  | GPIO4  | Hardware I2C                                                       |
| Button        | D3  | GPIO0  | INPUT_PULLUP, pressed = LOW; do not hold at boot                   |
| TM1637 DIO[1] | D4  | GPIO2  | Shares with INTERNAL_LED; both idle HIGH                           |
| Internal LED  | D4  | GPIO2  | Active-low; shared with TM1637 DIO[1] — LED flickers during write |

### Pin boot constraints
- **GPIO15 (D8)**: must be LOW - leave unconnected; any pull-up prevents boot/flash.
- **GPIO0 (D3)**: must be HIGH - INPUT_PULLUP + button not pressed.
- **GPIO2 (D4)**: must be HIGH - LED and TM1637 DIO both idle HIGH; safe.

### Serial / I2C / RTC
- Serial at 74880 baud for readable ESP8266 boot output.
- Initialize I2C early in `setup()` with explicit SDA/SCL pins before probing the RTC.
- `main.cpp` uses `TimeService` (`time_service.h/cpp`) as the loop-facing abstraction for second-tick consumption, cached time reads, log-interval checks, and RTC health checks.
- RTC SQW runs at 1Hz RISING interrupt. Call `rtcBeginSqwProcessing()` after `rtcBegin()`, then every loop call `rtcConsumeSqwPulse()` first - it returns `true` exactly once per real RTC second and advances the `rtcGetNowCached()` cache. Only when it returns `true`, optionally call `rtcIsLogIntervalDue()` to check whether the cached wall-clock second also lands on a `kSqwLogIntervalSeconds` boundary (`:00` and `:30`, i.e. every 30s) - meaning it's time for the throttled health/state log line. That call also resyncs the cache with a live read to correct for any missed pulses.
- **Do not gate time-sensitive logic on `rtcIsLogIntervalDue()`** — it's throttled to the `:00`/`:30` boundary and exists only to pace logging. Anything that needs to react promptly to the RTC crossing a boundary (e.g. Friday-mode phase transitions in `fridayModeTick()`) must gate on `rtcConsumeSqwPulse()` instead. Piggybacking on the log gate was a real bug once (Friday mode's phase change lagged the actual Thu→Fri midnight crossing by up to a minute) — see `main.cpp`'s loop for the corrected wiring.
- `rtcGetNow()` returns a `DateTime` from RTClib via a live I2C read; `rtcGetStatus()` returns `RtcStatus` (present, powerLost, lowBattery, sqwConfigured, error).
- **`rtcGetNowCached()`** — second-resolution `DateTime` maintained in software by `rtcConsumeSqwPulse()`: each SQW pulse advances a cached `DateTime` by one second (`cachedNow_.unixtime() + 1`) instead of re-reading the DS3231, since the pulse edge already tells you a second has elapsed. On the `:00`/`:30` boundary, `rtcIsLogIntervalDue()` resyncs the cache with one live `rtc.now()` read to correct for any pulses missed. If the SQW pulse goes stale (no pulse within `kSqwPulseStaleMs` = 3s, per the same freshness check `rtcIsHealthy()` uses) or the cache hasn't been seeded yet, `rtcGetNowCached()` transparently falls back to a live read — so it degrades gracefully rather than freezing if the interrupt/polling fallback ever stalls. `rtcSetNow()` immediately resyncs the cache to the new value so a manual time change doesn't wait for the next boundary to take effect.
- `RtcClockSource::now()` (`clock_source.cpp`) — the `ClockSource` used by `DisplayManager` for all render calls — uses `rtcGetNowCached()`, not `rtcGetNow()`. This matters because tenths-of-a-second formats re-render every 100ms (`renderElapsed()` gates at `kTenthMs`), which would otherwise mean up to 10 I2C transactions/sec for data that only actually changes once a second. Prefer `rtcGetNowCached()` for any new hot render/tick path; reserve `rtcGetNow()` for infrequent, correctness-critical one-off reads (e.g. `GET /api/time`).
- For fatal exception debugging, keep exception decoding enabled and include decoded stack traces in reports.

### Logging
- `log.h` provides `LOG_PRINTLN(msg)` and `LOG_PRINTF(fmt, ...)` macros.
- Each line is prefixed with `logCurrentTime()` and `logSourceName(__FILE__):__LINE__`.

### Display / mode architecture
The display system has four layers:

1. **`format.h/cpp`** - format-group tables, the `Mode` enum, and per-format metadata.
   - `FormatGroup` enum: `kFmtGroupCountdown`, `kFmtGroupCountUp`, `kFmtGroupClock` - low-level selector for which table of format strings to index into (`getFormat`/`getFormatMeta`/`formatCount`/`sanitizeFormatIndex`). Unrelated to the `Mode`/`View`/`Overlay` model below - it's purely a format-table lookup key.
   - `Mode` enum: `kModeCountdown`, `kModeCountup`, `kModeClock`, `kModeFriday` - the persisted mode stored in `ClockConfig.activeMode` and restored after any temporary overlay. See "Display / mode architecture" below for how this relates to `View` and `Overlay`.
   - `getFormat(group, index)` and `formatCount(group)` are the public accessors.
   - `FormatMetadata` struct holds `hasTenths` and `blinkColon` per entry. `getFormatMeta(group, index)` returns a pointer; `kFormatGroupMeta[group]` is the backing array in `format.cpp`. Always add a matching metadata row when adding or reordering a format string.
   - Predicates `countdownHasTenths`, `countupHasTenths`, `clockHasTenths`, `clockBlinkColon` (in `clock_format.h`) are table-driven via `FormatMetadata` — do NOT add hardcoded index comparisons.
  - Current clock tokens: `YYYY`, `MM`, `DD`, `DOW`, `hh`, `mm`, `ss`, `u`.
   - In clock formats, `H` and `N` are labels rendered as lowercase `h` and `n`.
   - A semicolon in a clock format, e.g. `hh;mm`, marks a blinking colon. Static duplicate clock formats have intentionally been removed.

2. **`clock_format.h/cpp`** - pure renderers (no I/O). Each fills three 4-char string buffers (r1/r2/r3), one per physical display.
   - `renderCountdown(idx, fields, r1, r2, r3)`, `renderCountup(idx, fields, r1, r2, r3)`, `renderClock(idx, fields, r1, r2, r3, colonVisible)`.
  - Rendering is plan-driven via `kCountingPlans` and `kClockPlans` row-op tables (not per-index switch/case blocks).
   - Internal fragment helpers in `clock_format.cpp`: `fmtNumber`, `fmtText`, `fmtMonthDay`, `fmtHourMin`, `fmtHourMinBlink`, `fmtMinSec`, `fmtBlankPadded`, `fmtZeroPadded`, `fmtDaysWithLabel`, `fmtDaysRight`. Use these for common patterns; do not add bare `snprintf` duplicates.
   - Numeric-only rows are right-justified across the full 4-character panel (`7` renders as `"   7"`).
   - For colon formats, the value left of the colon is blank-padded, not zero-padded (` 9:05`, not `09:05`).
   - When a blinking colon is off, render the time without a separator (` 905`) so all minute digits remain visible.
  - `clockFormatValidateInvariants()` validates format-table counts against plan-table counts at boot.
  - Intentional behavior: token labels in `format.cpp` intentionally differ from rendered labels in `clock_format.cpp`, and custom day abbreviations in `dowAbbrev()` are intentional.

3. **`display.h/cpp`** - `SegmentDisplay` singleton wrapping 3 `TM1637Display` objects.
   - `begin(brightness)`, `setBrightness(0-7)`, `showPanels(r1, r2, r3)`, `blank()`.
   - Panel strings are 4 chars; `:` or `;` between positions 1-2 lights the colon; `.` lights the decimal on the preceding digit.
   - Caches last-written segments per panel; skips hardware write on identical content.
   - ASCII-to-segment glyph mapping lives in `display.cpp` as `ASCII_SEGMENTS`; adjust that table when a letter does not display well on 7-segment hardware.

4. **`display_manager.h/cpp`** - `DisplayManager` singleton; the single entry point for display state and transitions. Three distinct concepts on purpose, each answering a different question:
   - **`Mode`** (`format.h`) - the persisted, user-selected setting. Answers "what did the user configure the clock to do." Stored in `ClockConfig.activeMode`.
   - **`View`** (`display_manager.h`) - what content is currently the normal thing to render: `kClock`, `kCountdown`, `kCountup`, each with its own payload (`ViewState`/`ViewPayload`, a tagged union like the old `DisplayPayload`: `countdown` (endTime, formatIndex), `countup` (startTime, formatIndex), `clock` (formatIndex)). For Countdown/Countup/Clock modes, `View` is fixed by the `Mode` (`viewForMode()` sets it once). **Friday mode is the one case where `View` changes on its own** - `FridayModeController` recomputes it as its phase changes and pushes the update via `setView()`.
   - **`Overlay`** (`display_manager.h`) - a temporary layer shown on top of the current `View`: `kDemo` (live countdown from `overlay_.transition.expiresAtMs`, no `formatIndex`, not stored in config), `kMessage`, `kPagedMessage` (`OverlayState`/`OverlayPayload`: `message[64]` or `paged`). Pushed by `showSplash`/`showDemo`/`showInfo`/`showPages`, popped by `clearOverlay()` or its own expiration. `PagedDisplayPayload` and `DisplayPage` have no default member initializers; callers set all fields explicitly.
   - Rendering rule, always: show the overlay if `hasOverlay_` is true, otherwise show `baseView_` (there is no third option). There is no separate "state to restore" snapshot — when an overlay clears, whatever `baseView_` *currently* is (possibly updated by Friday mode while the overlay was up) is what appears next. This is a deliberate change from the old `defaultState_`/`currentState_`/`previousState_` model, which stored a frozen snapshot to restore to and could go stale relative to `defaultState_` (a real bug we hit: a boot splash captured Friday mode's placeholder clock view *before* its first tick corrected it, and restored that stale snapshot instead of the corrected countdown once the splash cleared).
  - `begin(config)`, `applySettings(config)` (hot-reload, no reboot), `tick(nowMs)`.
  - Render timing/blink policy lives in `DisplayScheduler` (`display_scheduler.h/cpp`): render throttling, message/page blink cadence, and clock-colon blink cadence.
   - `setView(view)` — updates `baseView_`. If no overlay is active, also re-renders immediately. If one is active, nothing else needs to happen — the new view simply becomes visible once the overlay clears. Used by `FridayModeController` to switch phases.
   - `renderedName()` returns whatever's actually on the segments right now, as a string (the overlay's name if one is active, else the view's) — useful for logging.
   - `activeMode()` returns the persisted `Mode`; `activeView()` returns the `View` backing `baseView_` (not the overlay, so a transient splash/info/demo never counts as a view change). `main.cpp`'s periodic SQW log line and its mode/view transition log both use these two accessors, not `renderedName()`.
   - `viewName(View)` and `overlayName(Overlay)` are free functions declared in `display_manager.h` — the canonical name mappings for logging; do not duplicate them elsewhere.
   - A countdown reaching zero installs an `Overlay::kMessage` showing `finalMessage` with `transition.hasExpiration = false` — i.e. a permanent overlay, since the countdown has nothing further to show until the next mode/config change.
   - Clock colon blink toggles once per second (2-second full on/off cycle). Message/page blinking uses its own 500ms cadence.
   - `logTransition()` reasons: `"view install"` (boot/`applySettings()` installs `baseView_`), `"view update"` (a live `setView()` while no overlay is active, e.g. Friday's phase change), `"overlay"` (an overlay is pushed), `"overlay cleared"` (an overlay expires or is cleared and `baseView_` reappears).
   - When `ClockConfig.clockUse12Hour` is true, `renderClock()` converts `fields.hours` to 1–12 scale before calling `::renderClock()`. The conversion is local to that method and does not affect countdown/countup rendering.

5. **`clock_state.h`** - thin public API used by `web_server.cpp` to decouple it from `DisplayManager`.
   - `clockApplySettings(cfg)`, `clockSetBrightness(b)`, `clockTriggerDemo()`, `clockShowMessagePreview(msg)`, `clockShowInfo(msg, durationMs)`, `clockClearInfo()`.

### Friday Mode
- **`friday_mode.h/cpp`**: `FridayModeController` (internal singleton). Public API: `fridayModeApplySettings(config)` and `fridayModeTick(now)`.
- `fridayModeTick()` is called from `main.cpp` on every real SQW second (via `rtcConsumeSqwPulse()`, passing `rtcGetNowCached()`) — not on the throttled log-interval pulse; self-gates — does nothing unless `activeMode == kModeFriday`, and short-circuits when the phase hasn't changed.
- Phase logic (all times local, derived from device `location` + `utcOffsetMinutes`):
  - **Clock phase** (`kClock`): Saturday sunset through Friday midnight (i.e. all of Sun-Thu); also the default.
  - **To Friday sunset** (`kToFridaySunset`): Friday midnight → Friday sunset.
  - **To Saturday sunset** (`kToSaturdaySunset`): Friday sunset → Saturday sunset.
- Each phase transition calls `displayManager.setView()` with the appropriate `ViewState` (`View::kClock` or `View::kCountdown`). Format indices come from `ClockConfig.fridayClockFmt`, `fridayToFridaySunsetFmt`, `fridayToSatSunsetFmt`. This is the *only* mode where `View` moves on its own — see "Display / mode architecture" above.
- Sunset targets are cached in `FridayModeController` and recomputed at most once per week (when `fridayDateFor(now)` returns a different date than last time). `calculateSunset()` is **not** called on every tick.
- `fridayDateFor(now)`: returns midnight of the most recent Friday (or today if Friday). This value stays constant from Saturday through the following Thursday and only advances once `now` reaches the next Friday — that's what drives the once-per-week cache refresh and the Clock→countdown phase change in `computePhase()`. `computePhase()` never needs to check `cachedFridayDate_` directly: on Sat-Thu both cached sunsets are still last week's (already in the past), so it falls through to the default `kClock`.
- `applySettings()` resets the cached Friday date to `DateTime()` to force recomputation on the next tick (needed when location or UTC offset changes).

### Input
- **`button.h/cpp`**: `buttonBegin()`, `buttonTick()` (debounce), `buttonHasEvent()`, `buttonNextEvent()`.
  - `ButtonEvent` enum: `SHOW_SSID`, `SHOW_IP_ADDRESS`, `SHOW_RTC_STATUS`.
- **`page_manager.h/cpp`**: `PageManager` singleton. `showSsid(ssid)`, `showIpAddress(ip)` - builds `DisplayPage` arrays and hands them to `displayManager.showPages()`.

### Geography
- **`zipcode.h/cpp`**: `zipcodeLookupLocation(zipcode, &out, path)` - searches `/zipcodes.txt` on LittleFS; `isValidZipcode(zipcode)`.
- **`sunset_calculator.h/cpp`**: `calculateSunset(localDate, location)` - uses SolarCalculator to return a `DateTime` for local sunset given a `Location` (lat/lon/UTC offset).
  - SolarCalculator returns UTC hours. The code derives the UTC calculation date from the requested local sunset date by anchoring at 18:00 local, converts the returned UTC sunset to a UTC `DateTime`, then applies `utcOffsetMinutes` once to return local time.
  - The `dst` flag is persisted and echoed by APIs, but sunset math uses only numeric `utcOffsetMinutes`.

### Sunset Calculation Model
- Implementation: `sunset_calculator.cpp` (`calculateSunset`).
- Goal: return local sunset wall time for the requested local date and location.
- Algorithm:
  1. Validate coordinates (`latitude` in [-90, 90], `longitude` in [-180, 180]).
  2. Build `localEvening` at `18:00:00` on the requested local date.
  3. Convert that anchor to UTC (`utcDateForLocalSunsetDate`) by subtracting `utcOffsetMinutes`.
  4. Run `calcSunriseSunset` for `utcDate` and location.
  5. If sunset is NaN, return fallback local `18:00:00`.
  6. Convert sunset hours to rounded seconds.
  7. Build `utcSunset` as `utcMidnight + sunsetSeconds`.
  8. Convert back to local by adding `utcOffsetMinutes`.
- Fallback behavior:
  - Invalid coordinates -> local `18:00:00`.
  - NaN sunset -> local `18:00:00`.
- Timezone semantics:
  - Sunset math is offset-based (`utcOffsetMinutes`), not timezone-rule-based.
  - `dst` is informational/persisted but not directly consumed by the math.
- Integration:
  - Friday mode caches and uses these local sunset values as countdown targets in `friday_mode.cpp`.

### Storage / config
- `ClockConfig` (in `config.h`) holds: `activeMode`, format indices (`countdownFmt`, `countupFmt`, `clockFmt`), friday format indices (`fridayClockFmt`, `fridayToFridaySunsetFmt`, `fridayToSatSunsetFmt`), `countdownDatetime[20]`, `countupDatetime[20]`, `splashMessage[64]`, `finalMessage[64]`, `brightness`, `location` (`LocationInfo`), `sunsetTest` (`LocationInfo`), `timezone[40]`, `utcOffsetMinutes`, `dst`, `clockUse12Hour`.
- `clockUse12Hour` serializes as `display.clock12Hour` (boolean) in `/config.json`. Default `false` (24-hour). Applies to clock mode and the Friday mode clock phase; does not affect countdown/countup hours.
- `LocationInfo` struct (in `config.h`): `latitude`, `longitude`, `zipcode[6]`. Used for both `location` (physical device location) and `sunsetTest` (Sunset Calculator test inputs). Do not cross-read one for the other.
- `/config.json` has separate `location` and `sunset` objects. `location` is the physical device location. `sunset` stores Sunset Calculator test inputs. Do not use one as a backward-compatible fallback for the other.
- `WifiConfig` holds: `staSsid`, `staPassword`, `apSsid`, `apPassword`.
- `ConfigManager` singleton in `config.h/cpp`:
  - `loadClockConfig()` / `saveClockConfig(cfg)` - persisted to `/config.json` on LittleFS. Save reads the existing file, patches its section, and writes back (wifi section is preserved without a separate struct round-trip).
  - `loadWifiConfig()` / `saveWifiConfig(cfg)` - separate section of the same file, same single-read-patch-write pattern.
- **`config_serializer.h/cpp`** - shared JSON schema for `ClockConfig` and `WifiConfig`. Use these functions everywhere; do not duplicate JSON field names.
  - `serializeClockConfig(doc, config)` - writes display/time/location/sunset sections.
  - `serializeWifiConfig(doc, wifi)` - writes full wifi including passwords (for disk storage).
  - `serializeWifiStatus(doc, wifi)` - writes wifi without station password (for HTTP responses).
- **`config_validation.h/cpp`** - sanitize and convert config values. Canonical home of `modeName(mode)` and `modeFromName(name, out)`. Do not redeclare these elsewhere.
- **`config_api.cpp` + `config_update_service.cpp`** — `parseJsonBody(doc, route)` remains the private `ConfigApi` helper for request parsing. `handleSaveConfig()` delegates payload mapping + persistence orchestration to `ConfigUpdateService`, which keeps route handlers thin and avoids duplicating config-update logic.

### Networking
- **`wifi_connection_manager.h/cpp`**: `WifiConnectionManager` singleton.
  - `begin(config)`: tries STA first (if `staSsid` is set, 15s timeout); falls back to AP.
  - `tick()`: handles deferred events (e.g. AP client connected logging).
  - `status()` returns `WifiRuntimeStatus` (mode, connected, ssid, ip, apSsid, apIp).
  - `scanNetworks(doc)`, `connectAndSave(ssid, password)` for web-driven network switching.
- **`web_server.h/cpp`**: `webBegin()`, `webHandleClients()` (must be called every loop), `networkGetInfo(ssid, ip)`.
  - Runs `DNSServer` for captive portal only when in AP mode.
  - UI pages: `GET /`, `/settings`, `/config`, `/format`, `/time`, `/sunset`, `/messages`, `/location`, `/wifi`.
  - REST API: `POST /api/config`, `GET /api/config`, `GET /api/formats`, `POST /api/mode`, `POST /api/brightness`, `GET|POST /api/time`, `POST /api/sunset`, `GET /api/zipcode/lookup`, `POST /api/demo/test`, `POST /api/message/test`, `GET /api/wifi/status`, `GET /api/wifi/scan`, `POST /api/wifi/connect`.
  - File management: `GET /api/files`, `GET|DELETE /api/file`, `POST /api/file/upload`, `GET /view`.
- `/location` edits the persisted device `location`. `/sunset` edits/persists only the `sunset` test fields before posting to `/api/sunset`.
- On `/time`, "Set Time from Browser" updates the RTC/config and mirrors the new browser-derived values into the Device fields after a successful save.
- `webHandleClients()` must be called every loop iteration.
