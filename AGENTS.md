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
  ├── rtc_ds3231          – DS3231 driver; 1 Hz SQW interrupt drives the main tick
  ├── display (4 layers)
  │     format            – format-string tables + FormatMetadata
  │     clock_format      – pure renderers → three 4-char buffers
  │     display           – SegmentDisplay singleton (TM1637 hardware)
  │     display_manager   – DisplayManager singleton (all state/transitions)
  ├── friday_mode         – FridayMode controller; called each SQW pulse
  ├── config              – ConfigManager singleton (/config.json on LittleFS)
  │     config_serializer – shared JSON schema (single source of field names)
  │     config_validation – sanitization; owns persistentModeName helpers
  ├── wifi_connection_manager – STA → AP fallback; captive DNS in AP mode
  ├── web_server          – ESP8266WebServer; HTML pages + REST API
  │     clock_state.h     – thin shim decoupling web_server from DisplayManager
  ├── button / page_manager – debounced input; scrolling page display
  └── zipcode / sunset_calculator – geography helpers (LittleFS lookup + math)
```

## Persistent modes

`PersistentMode` (in `format.h`) is the value stored in config and restored after any temporary state:

| Value | Name | Behavior |
|-------|------|----------|
| `kPersistentCountdown` | countdown | Counts down to a configured target datetime |
| `kPersistentCountup`   | countup   | Counts up from a configured start datetime |
| `kPersistentClock`     | clock     | Displays current time |
| `kPersistentFriday`    | friday    | Thu midnight → countdown to Fri sunset → countdown to Sat sunset → clock (repeats) |

## Critical invariants

- **`DisplayPayload` is a union** — only the member matching `DisplayState::behavior` is valid. Never read a different union member.
- **Format metadata rows must stay in sync** — whenever a format string is added or reordered in `format.cpp`, a matching `FormatMetadata` row must be added in the same position. Index mismatches cause silent rendering bugs.
- **`config_serializer` is the single source of JSON field names** — do not duplicate field name strings elsewhere.
- **`location` vs `sunsetTest`** — `ClockConfig.location` is the physical device location used by friday_mode; `ClockConfig.sunsetTest` is the Sunset Calculator page's test input. Do not substitute one for the other.
- **`webHandleClients()` must be called every `loop()` iteration** — skipping it stalls the web server and DNS.
- **GPIO15 must stay LOW at boot** — do not add any pull-up on D8.
