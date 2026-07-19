# AGENTS.md

This file provides guidance to AI coding agents working in this repository. See [CLAUDE.md](CLAUDE.md) for the full reference on conventions, architecture, hardware pin map, and module APIs.

## Project overview

Embedded C++ firmware for a Wemos D1 Mini (ESP8266) clock with three TM1637 7-segment displays, a DS3231 RTC, WiFi, and a captive-portal web UI. Built with PlatformIO + Arduino framework; no host-side tests exist.

## Key build commands

```bash
pio run                          # compile
pio run --target upload          # compile + flash firmware
pio run --target uploadfs        # upload LittleFS (data/ directory)
pio device monitor               # serial monitor at 74880 baud
python tools/dev_server.py --device <clock-ip>   # edit web/ pages live, no reflash
```

## Architecture at a glance

```
main.cpp
  ├── rtc_ds3231          – DS3231 driver; 1 Hz SQW interrupt drives the main tick, an
  │                         application-owned RtcService, zero-I2C-cost cached time, and
  │                         an ISR-timestamped phase reference for tenths
  ├── display (layered)
  │     display_format    – declarative format catalog: FormatSpec = UI label + three
  │                         PanelSpec {Shape, Field, Field} triples → DisplayFrame;
  │                         RefreshRate/ColonAnimation are derived from the shapes
  │     display_renderer  – pure demo/message/page frame renderers (no I/O)
  │     display           – ClockApplication-owned SegmentDisplay (TM1637 hardware)
  │     display_manager   – owned state, transitions, blink/colon cadence, and render policy
  ├── schedule            – pure Friday/Trading boundary math; desktop-testable, no Arduino I/O
  ├── friday_mode         – application-owned FridayModeController; ticked every real SQW second via
  │                         RtcService::consumeSqwPulse(), NOT the throttled log pulse
  ├── trading_mode        – application-owned TradingModeController; weekday schedule; same
  │                         per-SQW-second tick contract as friday_mode
  ├── config              – ClockApplication-owned ConfigManager (/config.json on LittleFS)
  │     config_api        – REST endpoint handlers (ConfigApi) for /api/config and friends
  │     time_api          – RTC read and browser-time synchronization endpoints
  │     location_api      – ZIP lookup and sunset-calculator endpoints
  │     config_serializer – shared JSON schema (single source of field names)
  │     config_validation – sanitization; owns modeName/modeFromName helpers
  ├── wifi_connection_manager – ClockApplication-owned STA → AP fallback service
  ├── web_server          – application-owned WebPortal; static gzipped pages + REST API
  │     ClockController   – application actions shared by the loop and web APIs
  │     web/              – page sources (pages/*.html, common.css, common.js);
  │                         tools/build_web.py packages them into flash, and
  │                         tools/web_manifest.py owns the route → file map
  ├── button / page_manager – debounced input; scrolling page display
  └── zipcode / sunset_calculator – geography helpers (LittleFS lookup + math)
```

## Mode / View / Overlay

Three distinct concepts (`display_manager.h`), each answering a different question - do not conflate them:

- **`Mode`** (`config.h`) - the persisted, user-selected setting, stored in `ClockConfig.activeMode` and restored after any temporary overlay. "What did the user configure the clock to do."
- **`View`** (`display_manager.h`) - what content is currently the normal thing to render (`kClock`/`kCountdown`/`kCountup`, with payload). Fixed by `Mode` for countdown/countup/clock; Friday and Trading modes recompute it per their schedule and push updates via `setView()`.
- **`Overlay`** (`display_manager.h`) - a temporary layer on top of the current `View` (`kDemo`/`kMessage`/`kPagedMessage`), pushed by `showSplash`/`showDemo`/`showInfo`/`showPages` and popped by `clearOverlay()` or its own expiration.

Rendering rule, always: show the overlay if one is active, otherwise show the base view. There is no separate "previous state" snapshot to restore - see the critical-invariants note below on why that matters.

| `Mode` value | Name | Behavior |
|-------|------|----------|
| `kModeCountdown` | countdown | Counts down to a configured end datetime |
| `kModeCountup`   | countup   | Counts up from a configured start datetime |
| `kModeClock`     | clock     | Displays current time (24h or 12h per `clockUse12Hour`) |
| `kModeFriday`    | friday    | Clock phase (Sat sunset → Fri midnight) → countdown to Fri sunset → countdown to Sat sunset → repeats. A **live** Fri-sunset crossing blinks `messages.fridaySunset` for 5s (`showInfo` overlay); arriving there from boot/config-save does not. |
| `kModeTrading`   | trading   | Counts down to the next weekday 09:30 open or 16:00 close in Eastern local time. Live crossings blink `messages.tradingOpen` or `messages.tradingClose` for 5s; boot/config-save/time-sync arrival does not. Holidays and early closes are not modeled. |

## 12-hour clock mode

`ClockConfig.display.clockUse12Hour` (`display.clock12Hour` in JSON, default `false`) converts the hour to 1–12 scale in the pure display renderer. Countdown and countup modes are unaffected — their `hours` field is elapsed time, not a time of day.

## Sunset Computation

- Sunset is computed in `sunset_calculator.cpp` using SolarCalculator and returned as local wall-clock `DateTime`.
- Inputs are: local target date, latitude/longitude, and `utcOffsetMinutes`.
- The calculation date is anchored at local 18:00, then shifted to UTC before calling SolarCalculator. This avoids wrong-day results when local and UTC dates differ.
- SolarCalculator sunset hours are converted to rounded seconds, applied to UTC midnight for that UTC date, then shifted back to local using `utcOffsetMinutes`.
- Fallback behavior is deterministic:
  - invalid coordinates -> local `18:00:00`
  - NaN sunset from SolarCalculator -> local `18:00:00`
- `dst` is persisted/echoed by APIs but sunset math is driven only by numeric `ClockConfig.timezone.utcOffsetMinutes`.

## Critical invariants

- **`ViewState`/`OverlayState` are plain structs, not unions** — fields unused by the active view/overlay (e.g. `anchor` for clock, `message` for a paged overlay) are simply ignored. Do not reintroduce the old union-payload design.
- **Format declarations are the single source of truth** — each `FormatSpec` in `display_format.cpp` is a UI label plus three declarative `PanelSpec` shapes; the shapes are the only source of truth for rendering. `RefreshRate` and `ColonAnimation` are derived from the shapes (they cannot drift), and the `hhh:mm` overflow fallback is resolved semantically by `resolveCountingOverflow()` — no hardcoded indices. Countdown and countup intentionally share `kCountingFormats`.
- **Schedule math stays pure** — `schedule.h/cpp` contains Arduino-independent Friday/Trading boundary calculations. Controllers own cache/transition state and perform display actions; keep RTC, display, logging, and sunset I/O out of the pure schedule module.
- **Intentional token/render differences are required** — UI format tokens are intentionally different from rendered 7-segment labels, and the custom day abbreviations in `dayOfWeekAbbreviation()` are intentional. Do not normalize these unless explicitly requested.
- **The TM1637 panels have a center colon but no decimals** — `:`/`;` in a panel string are non-consuming colon markup handled by `renderPanelSegments()`. Do not add decimal-point parsing or use `.` as a separator.
- **`config_serializer` is the single source of JSON field names** — do not duplicate field name strings elsewhere.
- **Device location vs `sunsetTest`** — `ClockConfig.locations.device` is the physical device location used by friday_mode; `ClockConfig.locations.sunsetTest` is the Sunset Calculator page's test input. Do not substitute one for the other.
- **`WebPortal::handleClients()` must be called every `loop()` iteration** — skipping it stalls the web server and DNS.
- **Never build HTML on the server** — every page is a static gzipped PROGMEM asset generated from `web/` by `tools/build_web.py`; dynamic data flows through the JSON APIs (the home page uses `GET /api/status` for configured mode and live demo state). Add or rename routes only in `tools/web_manifest.py`. Shared page helpers belong in `web/common.js`, styles in `web/common.css` (both served hash-versioned and immutable).
- **AP-mode radio settings are evidence-backed** — the 11g phy mode, channel survey, and 17 dBm TX power in `wifi_connection_manager.cpp` fix observed transfer stalls with power-save phone clients; comments there record what was tried and what made things worse. Do not change them without new on-device evidence.
- **WiFi performance is only meaningful on the clock's own power supply** — confirmed 2026-07-14: USB-powered from the PC (supply droop + the PC's 2.4 GHz Bluetooth inches away), AP page transfers truncate or take ~18 s for 1.4 KB while device-side handling stays fast and heap healthy; on its own supply away from the PC the same firmware loads fast. Expect degraded AP throughput during USB bench debugging and do not chase it as a firmware bug. Discriminators: request-log `time=` = device-side cost, browser beacon `dl=` = radio delivery, `TRUNCATED wrote X of Y` = client stopped ACKing mid-transfer.
- **GPIO15 must stay LOW at boot** — do not add any pull-up on D8.
- **`setView()` vs `applySettings()`** — use `setView()` to update the base view without disturbing an active overlay (e.g. from the Friday or Trading controller). Use `applySettings()` only for full config reloads (it resets colon state and re-evaluates the full mode). Don't reintroduce a "previous state" snapshot to restore when an overlay clears — that pattern (the old `defaultState_`/`currentState_`/`previousState_` model) is what caused a real bug where a boot splash restored a stale pre-Friday-correction view instead of the live one. The current model has no snapshot: clearing an overlay just re-renders whatever `baseView_` currently is.
- **`RtcService::getNow()` vs `getNowCached()`** — `getNow()` is a live I2C read; `getNowCached()` is advanced by SQW pulses with a live-read fallback if pulses go stale. `DisplayManager` uses the cached version; do not replace it with live reads on the hot render path.
- **LOG macros require string literals** — `LOG_PRINTLN`/`LOG_PRINTF` keep their strings in flash (`PSTR` + `printf_P`) to hold static RAM under 50% for OTA. For a runtime string use `LOG_PRINTF("%s", value)`; a literal `%` in a `LOG_PRINTLN` message must be `%%` (it is pasted into the printf format). Each call appends its own newline, so formats must **not** end with `\n`.
- **Tenths are phase-locked to the RTC second** — compute them from `RtcService::msIntoSecond(nowMs)`, never `millis() % 1000`. `ClockController` notifies the display manager on each accepted SQW pulse.
- **`RtcService::consumeSqwPulse()` vs `isLogIntervalDue()`** — `consumeSqwPulse()` fires every real RTC second; `isLogIntervalDue()` is only true on `:00`/`:30` boundaries and only paces logging. Time-sensitive logic such as Friday and Trading phase transitions must gate on the real pulse.
