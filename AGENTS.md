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
```

## Architecture at a glance

```
main.cpp
  ├── rtc_ds3231          – DS3231 driver; 1 Hz SQW interrupt drives the main tick, a
  │                         zero-I2C-cost cached DateTime (rtcGetNowCached()), and the
  │                         phase reference for tenths (rtcMsIntoSecond(), ISR-timestamped)
  ├── display (4 layers)
  │     format            – format-string tables + FormatMetadata
  │     clock_format      – plan-driven pure renderers → three 4-char buffers
  │     display           – SegmentDisplay singleton (TM1637 hardware)
  │     display_manager   – DisplayManager singleton (state + transitions)
  │     display_scheduler – blink/colon cadence + render throttling policy
  ├── friday_mode         – FridayMode controller; ticked on every real SQW second via
  │                         rtcConsumeSqwPulse(), NOT the throttled log-interval pulse
  ├── config              – ConfigManager singleton (/config.json on LittleFS)
  │     config_api        – REST endpoint handlers (ConfigApi) for /api/config and friends
  │     config_serializer – shared JSON schema (single source of field names)
  │     config_validation – sanitization; owns modeName/modeFromName helpers
  ├── wifi_connection_manager – STA → AP fallback; captive DNS in AP mode
  ├── web_server          – ESP8266WebServer; HTML pages + REST API
  │     clock_state.h     – thin shim decoupling web_server from DisplayManager
  ├── button / page_manager – debounced input; scrolling page display
  └── zipcode / sunset_calculator – geography helpers (LittleFS lookup + math)
```

## Mode / View / Overlay

Three distinct concepts (`display_manager.h`), each answering a different question - do not conflate them:

- **`Mode`** (`format.h`) - the persisted, user-selected setting, stored in `ClockConfig.activeMode` and restored after any temporary overlay. "What did the user configure the clock to do."
- **`View`** (`display_manager.h`) - what content is currently the normal thing to render (`kClock`/`kCountdown`/`kCountup`, with payload). Fixed by `Mode` for the three non-Friday modes; Friday mode is the *one* case where it changes on its own, as `FridayModeController` recomputes it per its phase and pushes updates via `setView()`.
- **`Overlay`** (`display_manager.h`) - a temporary layer on top of the current `View` (`kDemo`/`kMessage`/`kPagedMessage`), pushed by `showSplash`/`showDemo`/`showInfo`/`showPages` and popped by `clearOverlay()` or its own expiration.

Rendering rule, always: show the overlay if one is active, otherwise show the base view. There is no separate "previous state" snapshot to restore - see the critical-invariants note below on why that matters.

| `Mode` value | Name | Behavior |
|-------|------|----------|
| `kModeCountdown` | countdown | Counts down to a configured end datetime |
| `kModeCountup`   | countup   | Counts up from a configured start datetime |
| `kModeClock`     | clock     | Displays current time (24h or 12h per `clockUse12Hour`) |
| `kModeFriday`    | friday    | Clock phase (Sun-Thu) → Fri midnight → countdown to Fri sunset → countdown to Sat sunset → repeats. A **live** Fri-sunset crossing blinks `messages.fridaySunset` for 5s (`showInfo` overlay); arriving there from boot/config-save does not. |

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

- **`ViewPayload`/`OverlayPayload` are unions** — only the member matching `ViewState::view` / `OverlayState::overlay` is valid. Never read a different union member.
- **Format metadata and renderer plans must stay in sync** — whenever a format string is added or reordered in `format.cpp`, keep `FormatMetadata` aligned and update plan tables in `clock_format.cpp` (`kCountingPlans` and `kClockPlans`) to the same shape/order. `clockFormatValidateInvariants()` catches count mismatches at boot, but not semantic row mistakes.
- **Intentional token/render differences are required** — format tokens in `format.cpp` are intentionally different from rendered labels in `clock_format.cpp`, and custom day abbreviations in `dowAbbrev()` are intentional. Do not normalize these unless explicitly requested.
- **`config_serializer` is the single source of JSON field names** — do not duplicate field name strings elsewhere.
- **Device location vs `sunsetTest`** — `ClockConfig.locations.device` is the physical device location used by friday_mode; `ClockConfig.locations.sunsetTest` is the Sunset Calculator page's test input. Do not substitute one for the other.
- **`webHandleClients()` must be called every `loop()` iteration** — skipping it stalls the web server and DNS.
- **GPIO15 must stay LOW at boot** — do not add any pull-up on D8.
- **`setView()` vs `applySettings()`** — use `setView()` to update the base view without disturbing an active overlay (e.g. from `FridayModeController`). Use `applySettings()` only for full config reloads (it resets colon state and re-evaluates the full mode). Don't reintroduce a "previous state" snapshot to restore when an overlay clears — that pattern (the old `defaultState_`/`currentState_`/`previousState_` model) is what caused a real bug where a boot splash restored a stale pre-Friday-correction view instead of the live one. The current model has no snapshot: clearing an overlay just re-renders whatever `baseView_` currently is.
- **`rtcGetNow()` vs `rtcGetNowCached()`** — `rtcGetNow()` is a live I2C read; `rtcGetNowCached()` is a second-resolution cache advanced by the SQW pulse in `rtcConsumeSqwPulse()`, with a live-read fallback if the pulse goes stale. `RtcClockSource` (used by `DisplayManager` for all rendering) uses the cached version — do not swap it back to `rtcGetNow()`, since tenths-of-a-second formats render every 100ms and would turn that into 10 I2C transactions/sec. Reserve `rtcGetNow()` for infrequent one-off reads.
- **LOG macros require string literals** — `LOG_PRINTLN`/`LOG_PRINTF` keep their strings in flash (`PSTR` + `printf_P`) to hold static RAM under 50% for OTA. For a runtime string use `LOG_PRINTF("%s\n", value)`; a literal `%` in a `LOG_PRINTLN` message must be `%%` (it is pasted into the printf format).
- **Tenths are phase-locked to the RTC second** — compute them from `rtcMsIntoSecond(nowMs)`, never `millis() % 1000` (unrelated phase). `main.cpp` calls `displayManager.notifySecondBoundary()` on each accepted SQW pulse so the first redraw of each second lands on the real boundary.
- **`rtcConsumeSqwPulse()` vs `rtcIsLogIntervalDue()`** — `rtcConsumeSqwPulse()` fires every real RTC second; `rtcIsLogIntervalDue()` is only true on the `:00`/`:30` wall-clock second boundary (`kSqwLogIntervalSeconds` = 30) and exists only to pace the periodic log line. Anything that must react promptly to the RTC (e.g. `fridayModeTick()`) has to gate on `rtcConsumeSqwPulse()`. This was a real bug once — `fridayModeTick()` was piggybacked on the log-interval gate, so Friday-mode phase transitions (e.g. Thu→Fri midnight) lagged the real crossing by up to a minute.
