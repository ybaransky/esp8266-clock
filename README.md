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
| HTTP route registration | `src/web_server.cpp` |
| JSON field name or config patching | `src/config_serializer.cpp` |
| Web page | `web/pages/` and `tools/web_manifest.py` |

## Build and test

```bash
pio run
pio test -e native
```

The native tests require a desktop C++ compiler (`gcc`/`g++`) on `PATH`. They
exercise Arduino-independent schedule logic and do not require a clock. Device
validation is still required for RTC, display, WiFi, and timing behavior.

Read `AGENTS.md` before hardware or timing changes; it contains the critical
electrical, RTC, display, storage, and network invariants. `CLAUDE.md` is the
full module reference.
