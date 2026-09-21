# Friday Mode Design

Status: implemented. The common boundary policy is described in
[scheduling-review.md](scheduling-review.md).

`schedule.cpp` evaluates the current view and next named boundary without I/O.
`ScheduledModeController` caches Friday/Saturday sunsets for the most recent
Friday and maps that decision to the existing display formats.

| Local interval | Base view | Next boundary |
| --- | --- | --- |
| Saturday sunset to Friday midnight | Clock | Friday midnight |
| Friday midnight to Friday sunset | Countdown | Friday sunset |
| Friday sunset to Saturday sunset | Countdown | Saturday sunset |

Sunsets use the physical device location and numeric UTC offset. Configuration
changes and RTC discontinuities invalidate the cache. Friday midnight is a
silent boundary. A live Friday sunset first installs the Saturday countdown,
then shows the configured message for five seconds and plays its song.

The common tracker accepts one boundary at most five seconds late. Boot,
configuration apply, time synchronization, and recovery rebase silently.
Approach alerts remain active when arriving inside their windows: Boundary 1
ends at Friday sunset and Boundary 2 at Saturday sunset.

The before/after blink windows both bracket Friday sunset. They are absolute
half-open intervals in `ViewState.blink`, resolved at render time, and introduce
no schedule phases. A zero-minute window is empty and disabled. The post-sunset
window continues beneath the temporary sunset message.
