# Friday Mode

This document describes the implemented Friday mode. The authoritative module
and invariant reference is [CLAUDE.md](CLAUDE.md).

## Behavior

Friday mode resolves the persisted mode into one of three base views:

1. Saturday sunset through Friday midnight: normal clock view.
2. Friday midnight through Friday sunset: countdown to Friday sunset.
3. Friday sunset through Saturday sunset: countdown to Saturday sunset.

At Saturday sunset the cycle returns to the clock view. All dates and targets
are local wall-clock values from the DS3231.

## Ownership

- `schedule.h/cpp` owns pure Friday phase and date-boundary calculations.
- `sunset_calculator.h/cpp` calculates local sunset from coordinates and the
  configured numeric UTC offset.
- `FridayModeController` owns cached sunset targets and remembered phase.
- `ClockController` applies configuration and ticks Friday mode on every
  accepted 1 Hz RTC SQW pulse.
- `DisplayManager` owns the base view and any temporary overlay.

The controller changes normal content with `DisplayManager::setView()`. It does
not clear or replace an active overlay; the new base view becomes visible when
that overlay ends.

## Live transition message

Crossing Friday sunset while the firmware is already running installs the
Saturday-sunset countdown and then blinks `messages.fridaySunset` for five
seconds. Boot, configuration reload, and browser time synchronization reset
the remembered phase to `kNone`, so merely arriving in that phase never
synthesizes a transition message.

## Location and cache

Friday mode uses `ClockConfig.locations.device`, never the Sunset Calculator
page's `locations.sunsetTest`. Sunset targets are cached by Friday date and are
invalidated by configuration changes or RTC synchronization.
