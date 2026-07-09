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
  ├── rtc_ds3231          – DS3231 driver; 1 Hz SQW interrupt drives the main tick and a
  │                         zero-I2C-cost cached DateTime (rtcGetNowCached())
  ├── clock_source        – ClockSource abstraction; DisplayManager reads time through this,
  │                         backed by rtcGetNowCached()
  ├── display (4 layers)
  │     format            – format-string tables + FormatMetadata
  │     clock_format      – pure renderers → three 4-char buffers
  │     display           – SegmentDisplay singleton (TM1637 hardware)
  │     display_manager   – DisplayManager singleton (all state/transitions)
  ├── friday_mode         – FridayMode controller; ticked on every real SQW second via
  │                         rtcConsumeSqwPulse(), NOT the throttled log-interval pulse
  ├── config              – ConfigManager singleton (/config.json on LittleFS)
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
| `kModeFriday`    | friday    | Clock phase (Sun-Thu) → Fri midnight → countdown to Fri sunset → countdown to Sat sunset → repeats |

## 12-hour clock mode

`ClockConfig.clockUse12Hour` (`display.clock12Hour` in JSON, default `false`) converts the hour to 1–12 scale in `DisplayManager::renderClock()` before passing fields to the pure renderer. Countdown and countup modes are unaffected — their `hours` field is elapsed time, not a time of day.

## Critical invariants

- **`ViewPayload`/`OverlayPayload` are unions** — only the member matching `ViewState::view` / `OverlayState::overlay` is valid. Never read a different union member.
- **Format metadata rows must stay in sync** — whenever a format string is added or reordered in `format.cpp`, a matching `FormatMetadata` row must be added in the same position in `kClockMeta` / `kCountdownMeta` / `kCountupMeta`. Index mismatches cause silent rendering bugs. The `renderClock()` switch in `clock_format.cpp` must also be updated to match.
- **`config_serializer` is the single source of JSON field names** — do not duplicate field name strings elsewhere.
- **`location` vs `sunsetTest`** — `ClockConfig.location` is the physical device location used by friday_mode; `ClockConfig.sunsetTest` is the Sunset Calculator page's test input. Do not substitute one for the other.
- **`webHandleClients()` must be called every `loop()` iteration** — skipping it stalls the web server and DNS.
- **GPIO15 must stay LOW at boot** — do not add any pull-up on D8.
- **`setView()` vs `applySettings()`** — use `setView()` to update the base view without disturbing an active overlay (e.g. from `FridayModeController`). Use `applySettings()` only for full config reloads (it resets colon state and re-evaluates the full mode). Don't reintroduce a "previous state" snapshot to restore when an overlay clears — that pattern (the old `defaultState_`/`currentState_`/`previousState_` model) is what caused a real bug where a boot splash restored a stale pre-Friday-correction view instead of the live one. The current model has no snapshot: clearing an overlay just re-renders whatever `baseView_` currently is.
- **`rtcGetNow()` vs `rtcGetNowCached()`** — `rtcGetNow()` is a live I2C read; `rtcGetNowCached()` is a second-resolution cache advanced by the SQW pulse in `rtcConsumeSqwPulse()`, with a live-read fallback if the pulse goes stale. `RtcClockSource` (used by `DisplayManager` for all rendering) uses the cached version — do not swap it back to `rtcGetNow()`, since tenths-of-a-second formats render every 100ms and would turn that into 10 I2C transactions/sec. Reserve `rtcGetNow()` for infrequent one-off reads.
- **`rtcConsumeSqwPulse()` vs `rtcIsLogIntervalDue()`** — `rtcConsumeSqwPulse()` fires every real RTC second; `rtcIsLogIntervalDue()` is only true on the `:00`/`:30` wall-clock second boundary (`kSqwLogIntervalSeconds` = 30) and exists only to pace the periodic log line. Anything that must react promptly to the RTC (e.g. `fridayModeTick()`) has to gate on `rtcConsumeSqwPulse()`. This was a real bug once — `fridayModeTick()` was piggybacked on the log-interval gate, so Friday-mode phase transitions (e.g. Thu→Fri midnight) lagged the real crossing by up to a minute.
