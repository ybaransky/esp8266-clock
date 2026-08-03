# Current Architecture

The earlier redesign described in this file has been implemented. This is a
compact map of the resulting architecture; [CLAUDE.md](CLAUDE.md) remains the
authoritative detailed reference.

## Application core

`ClockApplication` owns all stateful services and drives them from Arduino's
cooperative loop:

```text
ClockApplication
  |-- RtcService
  |-- SegmentDisplay
  |-- DisplayManager
  |-- ClockController
  |     |-- FridayModeController
  |     `-- TradingModeController
  |-- ConfigManager
  |-- WifiConnectionManager
  |-- WebPortal and domain APIs
  `-- PageManager
```

There are no application-wide service singletons. Dependencies are supplied by
the owning application object.

## Display model

Persisted `Mode`, live base `View`, and temporary `Overlay` are separate.
Scheduled controllers update the base view through `setView()` even while an
overlay is visible. Clearing the overlay reveals the latest base view; no
previous-state snapshot exists.

Formats are declarative `FormatSpec`/`PanelSpec` entries. Rendering metadata is
derived from panel shapes, and `SegmentDisplay` is the only TM1637 I/O layer.

## Scheduled modes

Pure Friday and Trading boundary calculations live in `schedule.h/cpp`.
Controllers own remembered phases, cached targets, display updates, logs, and
live-boundary announcements. Both are ticked from accepted 1 Hz RTC SQW pulses,
not from the throttled logging interval.

Trading uses a fixed-capacity `TradingSchedule`: two retained interval slots
and an enabled `intervalCount`. Session 1 is mandatory and session 2 optional.

## Configuration

`ConfigManager` owns a cached `DeviceConfig` and atomically persists the entire
document through a temporary file and rename. `config_serializer` owns JSON
field names and patch semantics. Clock and WiFi sections are preserved when the
other is updated.

## Web subsystem

Pages are normal files under `web/`, packaged as static gzipped PROGMEM assets
by `tools/build_web.py`. `tools/web_manifest.py` owns routes. Dynamic state uses
JSON APIs; handlers never assemble HTML.

## Validation workflow

The firmware is built with `pio run`. Hardware-sensitive behavior is validated
on the device using serial logs and the web UI.
