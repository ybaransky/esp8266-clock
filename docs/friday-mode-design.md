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

## Sunset blink windows

`friday.blinkBeforeMinutes` and `friday.blinkAfterMinutes` bracket **Friday**
sunset: the countdown blinks twice a second for the last N minutes before it
and the first M minutes after it. Either value at 0 disables that window.

The windows are not phases. The controller converts them into an absolute
`BlinkWindow` on the `ViewState` it installs - `[sunset - N, sunset)` on the
to-Friday-sunset countdown, `[sunset, sunset + M)` on the to-Saturday-sunset
countdown - and `DisplayManager` re-tests membership on every render, the same
way `longFormatIndex` is re-resolved. Consequences:

- No phase is added, so the `kToFridaySunset -> kToSaturdaySunset` crossing
  test that gates the sunset message is untouched.
- A window ends on its own; nothing has to push a new view to stop the blink.
- A time sync or a reboot inside a window lands in the right state with no
  crossing state to invalidate.

The post-sunset window starts under the 5-second sunset message overlay and
becomes visible when that overlay clears.
