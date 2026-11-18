# ESP8266 Clock — Project Overview

This document describes the **existing** project. It was checked against the source on 2026-11-17; still read the relevant code before modifying it.

Embedded C++ firmware for a Wemos D1 Mini (ESP8266), Arduino framework and PlatformIO. Three TM1637 7-segment displays share a clock line. A DS3231 RTC provides time and 1 Hz SQW timing. The clock includes Wi-Fi STA/AP fallback, a captive-portal web interface, LittleFS configuration, NTP correction, a buzzer, and several display modes (clock, countdown, count-up, Friday, Trading).

The project intentionally favors concrete constructor injection and pure, host-testable schedule and renderer functions over broad virtual-interface abstractions. Hardware testing is primary, supplemented by Unity native tests and Python pre-build validation.

## Source-of-truth locations

- Pin definitions: `src/hardware.h` (not this document).
- Wiring rationale: `docs/buzzer_wiring.md`.
- Sound behavior: `docs/sounds.md`.
- Actual environments and dependencies: `platformio.ini`.
- Config JSON schema: `config_serializer.h/cpp`.
- UI routes: `tools/web_manifest.py`.

See `architecture.md`, `hardware.md`, `invariants.md`, and `build-and-test.md`. The complete uploaded originals are retained under `docs/reference/` for audit purposes.
