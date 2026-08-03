# Friday Mode Design

Status: implemented current design.

Friday mode is deliberately split between pure schedule math and an
application-owned controller.

## Pure policy

`schedule.h/cpp` contains Arduino-independent helpers that determine the
Friday phase and most recent Friday midnight. The pure module performs no RTC,
display, logging, storage, or sunset I/O.

## Controller

`FridayModeController` snapshots only the configuration it needs, calculates
and caches Friday/Saturday sunset targets, remembers the installed phase, and
pushes `ViewState` changes through `DisplayManager::setView()`.

The controller is called once for every accepted RTC SQW second. It self-gates
unless `activeMode == kModeFriday` and avoids display work while the phase and
target remain unchanged.

## Phases

| Phase | Local interval | Base view |
|---|---|---|
| Clock | Saturday sunset to Friday midnight | Clock |
| To Friday sunset | Friday midnight to Friday sunset | Countdown |
| To Saturday sunset | Friday sunset to Saturday sunset | Countdown |

Friday midnight is based on RTC weekday convention. Sunset uses the physical
device location and `timezone.utcOffsetMinutes`.

## Transition semantics

Only a live `kToFridaySunset -> kToSaturdaySunset` crossing shows the configured
Friday-sunset message. A transition from `kNone` is initialization, not a live
event. Applying configuration and synchronizing time therefore cannot produce
a false boundary announcement.

The blinking message is an overlay. The Saturday countdown is installed first,
so clearing the message reveals the correct live view.
