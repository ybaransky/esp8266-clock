# ESP8266 Clock

Firmware for a Wemos D1 Mini clock with an RTC, three 4-digit displays, WiFi,
and a captive-portal configuration UI.

## The five-minute mental model

`ClockApplication` owns every stateful service. Arduino's `setup()` starts it
and `loop()` calls `tick()` continuously.

One loop iteration does this:

1. Read and dispatch button events.
2. Consume an RTC square-wave pulse, if one arrived.
3. Let `ClockController` update Friday or Trading schedules on that real-second
   boundary.
4. Let `DisplayManager` update transitions and render when due.
5. Service WiFi, HTTP, and captive DNS.

The display uses three separate concepts:

- `Mode` is the saved user choice.
- `View` is the clock/countdown/countup currently underneath.
- `Overlay` temporarily covers the view with a splash, demo, message, or pages.

An overlay never saves an old view. Scheduled modes may update the base view
while an overlay is visible; clearing the overlay reveals the latest base view.

```text
events + cached RTC time
          |
          v
  update application state
          |
          v
 pure schedule/format renderers
          |
          v
      DisplayFrame
          |
          v
 SegmentDisplay hardware write
```

## Where changes belong

| Change | Start here |
|---|---|
| Startup or main-loop behavior | `src/clock_application.cpp` |
| User action shared by loop and web | `src/clock_controller.cpp` |
| Clock/counting format | `src/display_format.cpp` |
| Display transition or overlay | `src/display_manager.cpp` |
| Friday/Trading boundary math | `src/schedule.cpp` |
| Friday sunset caching/announcement | `src/scheduled_mode.cpp` |
| Trading sessions/announcements | `src/scheduled_mode.cpp` |
| HTTP route registration | `src/web_server.cpp` |
| JSON field name or config patching | `src/config_serializer.cpp` |
| Web page | `web/pages/` and `tools/web_manifest.py` |

## Scheduled modes

Friday mode selects a clock or sunset countdown view from the current local
date and the configured device location. Trading mode counts down through one
or two ordered weekday sessions. Session 1 is always enabled; session 2 is
optional, and its times remain saved while disabled. Both controllers update
the base view once per accepted RTC square-wave second without disturbing an
active overlay.

Trading times are local wall-clock times. Holidays and early closes are not
modeled.

## References

Every document here tracks the code. Per-subsystem design records are not kept
separately: they duplicated CLAUDE.md, drifted out of step with it, and a reader
had no way to tell which copy was current. Git history holds the superseded ones.

- [CLAUDE.md](CLAUDE.md): the authoritative firmware reference - module APIs,
  conventions, and the reasoning behind the non-obvious decisions. Covers the
  display layering, scheduled modes, sound, storage, and web subsystems.
- [AGENTS.md](AGENTS.md): the critical invariants, in short form. Read this
  before hardware, timing, radio, or storage changes.
- [SOUNDS.md](SOUNDS.md): the `songs.bin` layout and the authoring workflow.
- [WIRING.md](WIRING.md): the buzzer's inverting NPN buffer and why D8 needs it.
- [clock.md](clock.md): PCB wiring brief, derived from `hardware.h`.
- [pcb.md](pcb.md): board specification - dimensions, mounting, connectors.

## Build

```bash
pio run                          # compile firmware
pio run --target upload          # compile + flash
pio run --target uploadfs        # upload the LittleFS image (data/)

# Host tests for the pure modules: schedule, display formats, datetime.
# Needs a host C++ compiler; PLATFORMIO_BUILD_DIR is required when a second
# PlatformIO install shares this project - see AGENTS.md.
PLATFORMIO_BUILD_DIR=$HOME/.cache/pio-build/esp8266-clock pio test -e native
```

Validate RTC, display, WiFi, schedule transitions, and timing behavior on the
device after relevant changes. The automated layers are narrower on purpose:
`tools/check_formats.py` guards the format catalog on every build, and `test/`
covers only the modules that are pure by design. Neither substitutes for
operating the clock.

Read `AGENTS.md` before hardware or timing changes; it contains the critical
electrical, RTC, display, storage, and network invariants. `CLAUDE.md` is the
full module reference.
