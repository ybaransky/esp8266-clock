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

## Time

The DS3231 keeps UTC. The timezone is an IANA zone chosen on `/time` and stored
with its POSIX daylight-saving rule, so the clock changes to and from daylight
time on its own. On the home network it also corrects itself from NTP; in
access-point mode the browser on `/time` is the time source.

## References

Documentation is organized under [docs/README.md](docs/README.md). The technical
documents were checked against the source on 2026-11-17; if code and docs
disagree, the code is authoritative.

- [CLAUDE.md](CLAUDE.md): the Claude Code entry point, which imports AGENTS.md.
- [AGENTS.md](AGENTS.md): the shared coding-agent instruction entry point.
- [architecture.md](docs/architecture.md): detailed module ownership and contracts.
- [invariants.md](docs/invariants.md): critical correctness rules. Read this
  before hardware, timing, radio, or storage changes.
- [sounds.md](docs/sounds.md): generated beeps, boundary patterns, and preview behavior.
- [buzzer_wiring.md](docs/buzzer_wiring.md): the buzzer's inverting NPN buffer and why D8 needs it.
- [pcb-specification.md](docs/pcb-specification.md): the board specification - bill of materials, pin
  assignments, net list, power tree, boot-strap constraints, and the fabrication
  parameters still to be decided.

## Build

```bash
pio run                          # compile firmware
pio run --target upload          # compile + flash
pio run --target uploadfs        # upload the LittleFS image (data/)

# Host tests for the pure modules: schedule, display formats, datetime, beep envelopes, timezone rules.
# Needs a host C++ compiler; PLATFORMIO_BUILD_DIR is required when a second
# PlatformIO install shares this project - see docs/build-and-test.md.
PLATFORMIO_BUILD_DIR=$HOME/.cache/pio-build/esp8266-clock pio test -e native
```

Validate RTC, display, WiFi, schedule transitions, and timing behavior on the
device after relevant changes. The automated layers are narrower on purpose:
`tools/check_formats.py` guards the format catalog on every firmware build, and `test/`
covers only the modules that are pure by design. Neither substitutes for
operating the clock.

Read [AGENTS.md](AGENTS.md) before changes and follow its required reading.
The critical electrical, RTC, display, storage, and network rules are in
[docs/invariants.md](docs/invariants.md); detailed module documentation is in
[docs/architecture.md](docs/architecture.md).
