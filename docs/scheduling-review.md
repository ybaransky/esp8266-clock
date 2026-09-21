# Scheduling and readability changes

Implemented 2026-09-20. Validation uses firmware compilation and the real clock;
no host test code or test target was added.

## One decision, one controller

`schedule.cpp` contains two pure calculations: `evaluateFridaySchedule()` and
`evaluateTradingSchedule()`. Each returns `ScheduleDecision`: what to show now
and the next named boundary. A boundary carries its kind, local wall-clock
seconds, and Trading session index. Exact boundaries belong to the following
interval, so the next target is in the future.

`ScheduledModeController` owns the shared previous-decision/time tracker, weekly
sunset cache, and mapping from decisions to views, messages, songs, and approach
alerts. It replaces the separate Friday/Trading controllers, `PhaseTracker`,
and `ModeOutputs`. There is no generic event queue or scheduling framework.

| Current interval | Display | Next boundary | Approach alert | Live announcement |
| --- | --- | --- | --- | --- |
| Saturday sunset to Friday midnight | Clock | Friday midnight | None | None |
| Friday midnight to Friday sunset | Countdown | Friday sunset | Boundary 1 | Friday message/song |
| Friday sunset to Saturday sunset | Countdown | Saturday sunset | Boundary 2 | None |
| Before a Trading session | Countdown | Session open | Boundary 1 for session 1 only | Open message/song |
| During a Trading session | Countdown | Session close | Boundary 2 for last session only | Close message/song |

Trading advances to the next weekday after the final close. Both session slots
remain persisted when session 2 is disabled. Friday uses the device location
and numeric UTC offset, never the sunset calculator page's test location.

A live crossing requires `previousTime < previousBoundary <= now`. It may be at
most five seconds late and must not have skipped another boundary. The tracker
evaluates at the old boundary to detect a second elapsed boundary without an
event queue. Boot, config apply, time sync, backward movement, RTC recovery, and
pulse backlogs install current state silently. Approach alerts remain eligible
inside their windows, even after a silent reset. A newly eligible approach alert
has priority over a boundary song if the windows overlap.

## Mode, view, and overlay remain separate

`ClockController` resolves the complete initial view before display settings
are applied. It captures count-up's `now` origin at config apply; that base-view
anchor survives time synchronization. There are no temporary schedule
placeholders in `DisplayManager`.

Ordinary countdown completion is application state. An accepted RTC sample
updates it regardless of whether the countdown is visible. Live completion
plays its cue once and replaces an ordinary information overlay. An expired
countdown on boot/config/time sync shows the final message silently. Moving
time back before the deadline rearms it.

The display renders completion as base presentation, so finishing a page or demo
returns to the completed message. Scheduled countdowns can reach zero while
waiting for a schedule sample without installing a permanent completion overlay.
Hardware faults use `showFault()`/`clearFault()` and retain priority over previews
and completion; clearing a fault reveals the current base content.

Overlay transitions now directly install or clear state, invalidate rendering,
and log the change. The mutation template and its boolean controls are gone.
Render invalidation uses an explicit flag rather than a zero timestamp, so it
also works across long uptime and `millis()` wrap.

Blink windows, over-24-hour formats, RTC-aligned tenths, declarative display
formats, and the hourly full segment rewrite remain in the presentation layer.

## RTC samples are coherent

`consumeSqwPulse(RtcTick&)` snapshots the pending count and latest ISR timestamp
together. One normal pulse advances cached time by one second. A backlog,
recovery, or first pulse performs one live read and marks a discontinuity,
instead of dropping elapsed seconds or replaying old announcements.

The same operation owns the normal :00/:30 resync before the sample reaches
scheduling. A correction marks a discontinuity. A fresh edge during I2C defers
the sample until the next loop; a stale queued edge waits for a fresh pulse.
The sample includes the phase reference used by approach alerts. Logging only
prints; `isLogIntervalDue()` and its hidden timekeeping side effect are removed.

## Validation and storage

`datetime_validation` checks real dates, including month lengths and leap years,
and times. Config accepts years 2000-2099 and `YYYY-MM-DD HH:MM[:SS]`, with a
space or T separator, then stores canonical seconds. Only count-up accepts
exactly `now`. The RTC sync and sunset APIs retain their 2020 minimum. Invalid
API dates are rejected; invalid disk fields retain defaults. Format indexes are
checked before conversion to `uint8_t`, so large inputs cannot wrap into a
valid index.

Boot tries a readable JSON-object `/config.json`, then `/config.bak`. A valid
backup is loaded even if restoring its filename fails. If neither is readable,
the application uses defaults in memory and preserves the files. Defaults are
written automatically only when both files are absent.

Saves serialize and verify `/config.tmp`, preserve the old file or surviving
backup, then install the replacement. The backup is removed only after install.
The sequence is recoverable across renames, rather than a single atomic
transaction. An abandoned temporary file is not treated as a committed save.

## Dead code and documentation

Removed the unused sound accessors, unused debug build flag, obsolete mode
controllers/tracker/output wrapper, render-driven completion event, unused
public overlay-clear method, mode placeholders, and transition template.
The documented alternative hardware tone backend remains intentional support.
Application services are injected; RTC/ISR state, storage, and the I2C scanner
retain documented module/global state.

## Validation on the clock

Compilation checks the firmware and packages the existing web/filesystem assets.
It cannot confirm RTC edge alignment or physical display/buzzer behavior.
After uploading, the useful checks are:

- A short ordinary countdown finishes once while an information page is visible.
- Friday midnight, both sunsets, and one/two Trading sessions select the next view.
- Saving settings or synchronizing time across a boundary does not replay its cue.
- Boot/time sync inside an approach window resumes the alert at the right point.
- Fault recovery reveals the current view; the hourly rewrite does not blank it.
- Invalid calendar dates are rejected and saved settings survive reboot.

WiFi performance should still be judged on the clock's own supply, as recorded
in the hardware guidance. No firmware or filesystem upload was performed as part
of this implementation.
