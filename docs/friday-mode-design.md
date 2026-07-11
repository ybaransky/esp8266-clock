# Friday Mode Design

## Goal

Friday mode counts down through a repeating three-step cycle:

1. Count down to sunset on Friday.
2. Once Friday sunset is reached, count down to the next sunset on Saturday.
3. Once Saturday sunset is reached, count down to the following Friday sunset.

Optionally, the third step can behave differently:

3. Once Saturday sunset is reached, show the normal clock until Friday, then start counting down to Friday sunset.

The mode needs the current location:

- Latitude
- Longitude

It may also need timezone handling, depending on whether the RTC stores local time or UTC.

## Recommendation

Do not start with a generic schedule/actions/events system.

Also avoid adding more overlay modes.

Instead, move toward explicit display states managed by a display manager / mode engine.

Each state should know:

- What it renders
- Whether it blinks

The manager should know:

- Whether the current state expires
- When it expires
- What state should replace it

Friday mode is deterministic and mostly answers one question:

```text
What sunset should the display count down to right now?
```

But the result should be expressed as a display state and transition, not as another special overlay branch.

## Proposed Shape

Add a new display mode:

```cpp
kModeFriday
```

Create a focused module:

```text
src/friday_mode.h
src/friday_mode.cpp
```

Suggested Friday model:

```cpp
enum class FridayPhase {
  CountdownToFridaySunset,
  CountdownToSaturdaySunset,
  CountdownToNextFridaySunset,
  ShowClockUntilFriday,
};

struct Location {
  float latitude;
  float longitude;
};

struct FridayTarget {
  DateTime sunset;
  FridayPhase phase;
};
```

Core function:

```cpp
FridayTarget computeFridayTarget(DateTime now, Location location);
```

Suggested display-state model:

```cpp
enum class DisplayBehavior {
  Clock,
  Countdown,
  Countup,
  Message,
};

struct DisplayState {
  DisplayBehavior behavior;
  bool blink;
  bool scroll;
  DisplayPayload payload;
};

struct DisplayTransition {
  bool hasExpiration;
  DateTime expiresAt;
};
```

In this model, Friday mode does not create an overlay. It provides Friday-specific phase and target information. The display manager uses that information to select the current display state and decide when to transition.

## Target Rules

```text
Friday before sunset      -> target today's Friday sunset
Friday after sunset       -> target Saturday sunset
Saturday before sunset    -> target today's Saturday sunset
Saturday after sunset     -> target next Friday sunset
Sunday through Thursday   -> target next Friday sunset
```

Optional clock-after-Saturday behavior:

```text
Friday before sunset      -> target today's Friday sunset
Friday after sunset       -> target Saturday sunset
Saturday before sunset    -> target today's Saturday sunset
Saturday after sunset     -> show normal clock until Friday
Sunday through Thursday   -> show normal clock until Friday
Friday before sunset      -> resume countdown to Friday sunset
```

## State Transition Integration

The clock engine should move away from overlay-style modes and toward state transitions.

The Friday flow becomes:

```text
Countdown to Friday sunset
  -> Countdown to Saturday sunset
  -> Countdown to next Friday sunset
```

With the optional clock-after-Saturday behavior enabled:

```text
Countdown to Friday sunset
  -> Countdown to Saturday sunset
  -> Clock until Friday
  -> Countdown to Friday sunset
```

The demo flow can use the same mechanism:

```text
Countdown for 5 seconds
  -> Blinking final message for 5 seconds
  -> Previous state
```

Info mode can also become a normal state:

```text
Message, optionally blinking or scrolling
  -> Previous state or Clock
```

`tickFriday()` no longer needs to act like a special overlay. The display manager should only resolve the next state when the current state expires or when the date crosses a relevant boundary.

Countdown rendering still follows the existing display path:

1. Read current RTC time.
2. Load/use configured latitude and longitude.
3. Resolve the current Friday phase.
4. Let the display manager create/update the current display state and transition metadata.
5. Render the current state normally.

Conceptual shape:

```cpp
struct FridayResolvedState {
  DisplayState state;
  DisplayTransition transition;
};

FridayResolvedState resolveFridayState(DateTime now, Location location) {
  const FridayTarget target = fridayMode.targetFor(now, settings_.locations.device);

  if (target.phase == FridayPhase::ShowClockUntilFriday) {
    return FridayResolvedState{
      .state = DisplayState{
        .behavior = DisplayBehavior::Clock,
        .blink = false,
        .scroll = false,
        .payload = ClockPayload{},
      },
      .transition = DisplayTransition{
        .hasExpiration = true,
        .expiresAt = nextFridayStart(now),
      },
    };
  }

  return FridayResolvedState{
    .state = DisplayState{
      .behavior = DisplayBehavior::Countdown,
      .blink = false,
      .scroll = false,
      .payload = CountdownPayload{target.sunset},
    },
    .transition = DisplayTransition{
      .hasExpiration = true,
      .expiresAt = target.sunset,
    },
  };
}
```

The display manager owns the question of what happens after expiration. For Friday mode, that usually means asking the Friday resolver again with the current time and installing the newly resolved state.

## Configuration

Persist location settings:

```cpp
float latitude;
float longitude;
```

Optional behavior setting:

```cpp
bool fridayModeShowsClockAfterSaturday;
```

Consider adding:

```cpp
int timezoneOffsetMinutes;
```

This depends on the RTC policy:

- If RTC stores local wall-clock time, latitude and longitude may be enough.
- If RTC stores UTC, Friday/Saturday boundaries require timezone conversion.

## Sunset Calculation

Keep sunset math isolated:

```cpp
DateTime calculateSunset(DateTime localDate, Location location);
```

This keeps Friday mode readable and allows the algorithm or library to be replaced later without changing the clock mode engine.

## Why Not A Full Scheduler Yet

A general schedule/actions/events system would make sense later if the project needs rules like:

```text
At sunset: show a message
At midnight: dim the display
On weekdays: show clock mode
On holidays: show custom messages
```

But Friday mode does not need that level of abstraction yet.

The recommended middle ground is:

```text
State transitions, not overlays.
Focused target resolvers, not a generic scheduler.
```

This gives enough flexibility for expiration, blinking, messages, and Friday transitions without turning the project into a broad rules engine too early.

## Summary

Recommended design:

```text
Friday mode = target resolver + display states + transitions + sunset calculator
```

Not yet:

```text
Generic scheduler + actions + events
```

Avoid:

```text
More overlay modes
```
