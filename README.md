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
| Friday sunset caching/announcement | `src/friday_mode.cpp` |
| Trading sessions/announcements | `src/trading_mode.cpp` |
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

## Design references

- [CLAUDE.md](CLAUDE.md): detailed authoritative firmware reference.
- [Display state](docs/display-state-design.md): Mode/View/Overlay ownership.
- [Friday mode](docs/friday-mode-design.md): sunset phases and live crossing.
- [Trading mode](docs/trading-mode-design.md): session persistence, validation,
  boundary selection, and announcements.
- [Web subsystem](docs/web-subsystem-redesign.md): static asset and API model.
- [WiFi management](docs/wifi-connection-management-design.md): STA/AP ownership.
- [clock.md](clock.md): authoritative PCB wiring derived from `hardware.h`.

## Build

```bash
pio run
```

Validate RTC, display, WiFi, schedule transitions, and timing behavior on the
device after relevant changes.

Read `AGENTS.md` before hardware or timing changes; it contains the critical
electrical, RTC, display, storage, and network invariants. `CLAUDE.md` is the
full module reference.
