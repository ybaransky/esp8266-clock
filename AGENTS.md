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
  ├── schedule            – pure Friday/Trading boundary math; no Arduino I/O
  ├── scheduled_mode      - one ScheduledModeController for Friday/Trading;
  │                         named boundaries, shared crossing policy, weekly sunset cache
  ├── datetime_validation - strict shared calendar and datetime parsing
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
- **`Overlay`** (`display_manager.h`) - a temporary layer on top of the current `View` (splash/message/demo/pages/hardware fault), pushed by `showSplash`/`showDemo`/`showInfo`/`showPages` and removed by expiration or config apply. Hardware faults use `showFault()`/`clearFault()` and take priority. Countdown completion is base presentation, not an overlay.

Rendering rule, always: show the overlay if one is active, otherwise show the base view. There is no separate "previous state" snapshot to restore - see the critical-invariants note below on why that matters.

| `Mode` value | Name | Behavior |
|-------|------|----------|
| `kModeCountdown` | countdown | Counts down to a configured end datetime |
| `kModeCountup`   | countup   | Counts up from a configured start datetime |
| `kModeClock`     | clock     | Displays current time (24h or 12h per `clockUse12Hour`) |
| `kModeFriday`    | friday    | Clock phase (Sat sunset → Fri midnight) → countdown to Fri sunset → countdown to Sat sunset → repeats. A **live** Fri-sunset crossing blinks `messages.fridaySunset` for 5s (`showInfo` overlay); arriving there from boot/config-save does not. `friday.blinkBeforeMinutes`/`blinkAfterMinutes` blink the countdown 2x/sec for N minutes before and M minutes after Fri sunset (0 = off) via `ViewState::blink`, not via a phase. |
| `kModeTrading`   | trading   | Counts down through one or two configured weekday trading sessions in local wall-clock time: each enabled start, each stop, then session 1 on the next weekday. Live crossings blink `messages.tradingOpen` or `messages.tradingClose` for 5s; boot/config-save/time-sync arrival does not. Holidays and early closes are not modeled. |

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
- Sunset math is driven by numeric `ClockConfig.timezone.utcOffsetMinutes`; the timezone name is informational and supports browser synchronization.

## Critical invariants

- **Duration-dependent presentation is resolved at render time, not by a phase** — `ViewState::longFormatIndex` and `ViewState::blink` are re-evaluated on every render, so they end on their own and survive time syncs/reboots with no crossing state. Do not add schedule phases for presentation-only changes.
- **`ViewState`/`OverlayState` are plain structs, not unions** — fields unused by the active view/overlay (e.g. `anchor` for clock, `message` for a paged overlay) are simply ignored. Do not reintroduce the old union-payload design.
- **Format declarations are the single source of truth** — each `FormatSpec` in `display_format.cpp` is a UI label plus three declarative `PanelSpec` shapes; the shapes are the only source of truth for rendering. `RefreshRate` and `ColonAnimation` are derived from the shapes (they cannot drift), and the `hhh:mm` overflow fallback is resolved semantically by `resolveCountingOverflow()` — no hardcoded indices. Countdown and countup intentionally share `kCountingFormats`.
- **Schedule math stays pure** — `schedule.h/cpp` contains Arduino-independent Friday/Trading boundary calculations. Controllers own cache/transition state and perform display actions; keep RTC, display, logging, and sunset I/O out of the pure schedule module.
- **Trading schedule shape** — `TradingSchedule` is a fixed-capacity array of two `TradingInterval` values plus `intervalCount`. Session 1 is always enabled. Both slots persist even when session 2 is disabled; enabled sessions must be ordered, non-overlapping, and separated by a gap. `isValidTradingSchedule()` owns these pure invariants.
- **Intentional token/render differences are required** — UI format tokens are intentionally different from rendered 7-segment labels, and the custom day abbreviations in `dayOfWeekAbbreviation()` are intentional. Do not normalize these unless explicitly requested.
- **The TM1637 panels have a center colon but no decimals** — `:`/`;` in a panel string are non-consuming colon markup handled by `renderPanelSegments()`. Do not add decimal-point parsing or use `.` as a separator.
- **`config_serializer` is the single source of JSON field names** — do not duplicate field name strings elsewhere.
- **Device location vs `sunsetTest`** — `ClockConfig.locations.device` is the physical device location used by scheduled_mode; `ClockConfig.locations.sunsetTest` is the Sunset Calculator page's test input. Do not substitute one for the other.
- **`WebPortal::handleClients()` must be called every `loop()` iteration** — skipping it stalls the web server and DNS.
- **Never build HTML on the server** — every page is a static gzipped PROGMEM asset generated from `web/` by `tools/build_web.py`; dynamic data flows through the JSON APIs (the home page uses `GET /api/status` for configured mode and live demo state). Add or rename routes only in `tools/web_manifest.py`. Shared page helpers belong in `web/common.js`, styles in `web/common.css` (both served hash-versioned and immutable).
- **AP-mode radio settings are evidence-backed** — the 11g phy mode, channel survey, and 17 dBm TX power in `wifi_connection_manager.cpp` fix observed transfer stalls with power-save phone clients; comments there record what was tried and what made things worse. Do not change them without new on-device evidence.
- **WiFi performance is only meaningful on the clock's own power supply** — confirmed 2026-07-14: USB-powered from the PC (supply droop + the PC's 2.4 GHz Bluetooth inches away), AP page transfers truncate or take ~18 s for 1.4 KB while device-side handling stays fast and heap healthy; on its own supply away from the PC the same firmware loads fast. Expect degraded AP throughput during USB bench debugging and do not chase it as a firmware bug. Discriminators: request-log `time=` = device-side cost, browser beacon `dl=` = radio delivery, `TRUNCATED wrote X of Y` = client stopped ACKing mid-transfer.
- **GPIO15 must stay LOW at boot** — do not add any pull-up on D8.
- **`setView()` vs `applySettings()`** — use `setView()` to update the base view without disturbing an active overlay (e.g. from `ScheduledModeController`). Use `applySettings()` only for full config reloads (the application supplies the resolved initial view and it resets presentation cadence). Don't reintroduce a "previous state" snapshot to restore when an overlay clears — that pattern (the old `defaultState_`/`currentState_`/`previousState_` model) is what caused a real bug where a boot splash restored a stale pre-Friday-correction view instead of the live one. The current model has no snapshot: clearing an overlay just re-renders whatever `baseView_` currently is.
- **`RtcService::getNow()` vs `getNowCached()`** — `getNow()` is a live I2C read; `getNowCached()` is advanced by SQW pulses with a live-read fallback if pulses go stale. `DisplayManager` uses the cached version; do not replace it with live reads on the hot render path.
- **LOG macros require string literals** — `LOG_PRINTLN`/`LOG_PRINTF` keep their strings in flash (`PSTR` + `printf_P`) to hold static RAM under 50% for OTA. For a runtime string use `LOG_PRINTF("%s", value)`; a literal `%` in a `LOG_PRINTLN` message must be `%%` (it is pasted into the printf format). Each call appends its own newline, so formats must **not** end with `\n`.
- **Tenths are phase-locked to the RTC second** — compute them from `RtcService::msIntoSecond(nowMs)`, never `millis() % 1000`. `ClockController` notifies the display manager on each accepted SQW pulse.
- **The display cache is invalidated at every wall-clock `:00:00`** — the next normal render rewrites all four digits on all three TM1637 panels, correcting a segment whose hardware state drifted from the software cache without visibly blanking the display. Keep the hourly trigger on the real SQW path.
- **RTC servicing owns resynchronization** - `consumeSqwPulse(RtcTick&)` supplies coherent cached time and an ISR phase reference after any required :00/:30 resync. It consumes pulse backlogs together and marks discontinuities; the controller rebases silently. Logging never changes time. Keep schedules on real RTC samples and the minute log gated only on `second() == 0`.
- **Boundary approach alerts stay non-blocking and RTC-aligned** — each boundary owns a tone, total duration, and starting beep rate; `SoundPlayer` divides the total into four equal phases whose rates double three times. Beeps are capped at 100 ms and otherwise use half the pulse period, with the tone at the end of each slot so the final beep ends on the boundary. Boundary 1 (default 880 Hz, 40 seconds, 2 beats/sec) is the first Trading open and Friday sunset; Boundary 2 (default 1320 Hz, 40 seconds, 2 beats/sec) is the last Trading close and Saturday sunset. Controllers update the armed target on every real SQW second; do not turn the alert into schedule phases or blocking delays.
- **Schedule decisions name boundaries** - pure Friday/Trading functions return current view kind plus next boundary kind/time/session. The shared controller announces only one live boundary, at most five seconds late. Boot/config/time sync/recovery and multi-boundary gaps are silent. Presentation windows are not schedule phases.
- **Countdown completion belongs to ClockController** - rendering never detects completion. Ordinary countdowns finish on accepted RTC samples even under overlays; faults retain display priority. Scheduled countdowns cannot install a permanent completion message.
- **Configuration recovery precedes defaults** - load the primary JSON object, then its backup. Keep unreadable files if neither loads; retain a surviving backup until a verified replacement is installed. Validate dates through `datetime_validation` and keep JSON names in `config_serializer`.
- **Validation uses the actual application** - do not add host-side test code or a test target. Compile with `pio run`; runtime verification is by uploading and operating the clock.
