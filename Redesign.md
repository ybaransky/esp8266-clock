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
  |     `-- ScheduledModeController
  |-- ConfigManager
  |-- WifiConnectionManager
  |-- WebPortal and domain APIs
  `-- PageManager
```

Application services receive their dependencies from the owning object. RTC
hardware/SQW state remains file-static for its ISR bridge; storage and the I2C
scanner also retain small global services.

## Display model

Persisted `Mode`, live base `View`, and temporary `Overlay` are separate.
Scheduled controllers update the base view through `setView()` even while an
overlay is visible. Clearing the overlay reveals the latest base view; no
previous-state snapshot exists.

Formats are declarative `FormatSpec`/`PanelSpec` entries. Rendering metadata is
derived from panel shapes, and `SegmentDisplay` is the only TM1637 I/O layer.

## Scheduled modes

Pure Friday and Trading calculations return a `ScheduleDecision`: current view
kind and next named boundary. One `ScheduledModeController` owns the sunset
cache, previous decision/time, output mapping, and shared crossing policy.
It is called on coherent `RtcTick` samples. RTC servicing handles backlog
recovery and 30-second resync; logging has no timekeeping side effects.

The application resolves initial views and owns ordinary countdown completion,
including under overlays. Hardware faults have explicit display priority.
Trading retains both configured session slots even when session 2 is disabled.

## Configuration

`ConfigManager` owns a cached `DeviceConfig` and persists the entire
document through a verified temporary file and recoverable backup/rename
sequence. Boot tries the primary, then the backup, before using defaults. `config_serializer` owns JSON
field names and patch semantics. Clock and WiFi sections are preserved when the
other is updated.

## Web subsystem

Pages are normal files under `web/`, packaged as static gzipped PROGMEM assets
by `tools/build_web.py`. `tools/web_manifest.py` owns routes. Dynamic state uses
JSON APIs; handlers never assemble HTML.

## Validation workflow

The firmware is built with `pio run`. Hardware-sensitive behavior is validated
on the device using serial logs and the web UI.

The implemented policy and device validation notes are in
[Scheduling review](docs/scheduling-review.md). No host test code is required.
