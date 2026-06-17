# Display State Design

## Goal

Replace special overlay-style behavior with one consistent display-state model.

In this design, the system always renders exactly one current display state. Temporary behavior, blinking, messages, demos, countdown completion, and Friday-mode phase changes are handled through state transitions, not overlays.

## Core Idea

Separate rendering from lifecycle.

```text
DisplayState      = what to render right now
DisplayBehavior   = the kind of rendering to perform
DisplayTransition = when the current state expires
DisplayManager    = owns the active state and chooses the next one
```

No display state should decide what comes after itself. That decision belongs to the display manager or to a mode-specific resolver called by the manager.

## Display Behavior

`DisplayBehavior` describes the rendering strategy.

```cpp
enum class DisplayBehavior {
  Clock,
  Countdown,
  Countup,
  Message,
};
```

Possible meanings:

```text
Clock     -> render current RTC time
Countdown -> render time remaining until a target DateTime
Countup   -> render elapsed time since a start DateTime
Message   -> render fixed text
```

## Display State

`DisplayState` describes what should appear on the display.

It should not know:

- When it expires
- What comes next
- Why it was selected

Suggested shape:

```cpp
struct DisplayState {
  DisplayBehavior behavior;
  bool blink;
  DisplayPayload payload;
};
```

`DisplayPayload` can be a tagged payload or a small struct with fields used by different behaviors.

Conceptually:

```cpp
struct DisplayPayload {
  DateTime endTime;      // Countdown
  DateTime startTime;    // Countup
  char message[64];      // Message
  uint8_t formatIndex;   // Clock/countdown/countup format
};
```

If the implementation wants stronger typing later, this can become separate payload structs:

```cpp
struct ClockPayload;
struct CountdownPayload;
struct CountupPayload;
struct MessagePayload;
```

## Display Transition

`DisplayTransition` describes the lifecycle of the current state.

```cpp
struct DisplayTransition {
  bool hasExpiration;
  DateTime expiresAt;
};
```

The transition does not contain `nextState`.

When a state expires, the display manager asks the active mode/resolver what state should be shown next.

This keeps transition ownership centralized:

```text
State renders.
Manager transitions.
Mode resolver decides mode-specific intent.
```

## Display Manager

The display manager owns:

- Current display state
- Current transition metadata
- Previous state, if needed
- Mode-specific resolver selection

Conceptual shape:

```cpp
class DisplayManager {
public:
  void begin(const ClockSettings& settings);
  void applySettings(const ClockSettings& settings);
  void tick(DateTime now, uint32_t nowMs);

private:
  DisplayState currentState_;
  DisplayTransition currentTransition_;
  DisplayState previousState_;
  ClockSettings settings_;

  void resolveCurrentState(DateTime now);
  void renderCurrentState(DateTime now, uint32_t nowMs);
  bool currentStateExpired(DateTime now) const;
};
```

Main tick flow:

```cpp
void DisplayManager::tick(DateTime now, uint32_t nowMs) {
  if (currentStateExpired(now)) {
    resolveCurrentState(now);
  }

  renderCurrentState(now, nowMs);
}
```

## Handling Current Modes

### Clock

Clock is a non-expiring state.

```text
behavior: Clock
blink: depends on selected clock format
transition: none
```

The renderer updates from RTC time.

### Countdown

Countdown is a state with a target time.

```text
behavior: Countdown
payload.endTime: configured countdown end time
transition.expiresAt: target time
```

When it expires, the manager resolves the next state.

For a normal countdown, the next resolved state may be:

```text
Message using finalMessage
```

That final-message state may either be permanent or have its own expiration, depending on desired behavior.

### Countup

Countup is usually non-expiring.

```text
behavior: Countup
payload.startTime: configured start time
transition: none
```

It continuously renders elapsed time since `startTime`.

### Message / Info

Info mode is a message state.

```text
behavior: Message
payload.message: runtime infoMessage
blink: optional
transition: optional
```

`infoMessage` is runtime-only and should not be saved to disk.

If Info is temporary, the manager can preserve `previousState_` and restore it when the message expires.

### Demo

Demo becomes a sequence of ordinary states.

```text
Countdown for 5 seconds
  -> Blinking finalMessage for 5 seconds
  -> Previous state
```

No overlay is needed.

The manager records the previous state before installing the demo sequence.

### Friday Mode

Friday mode is a mode-specific resolver that produces ordinary display states.

Default cycle:

```text
Countdown to Friday sunset
  -> Countdown to Saturday sunset
  -> Countdown to next Friday sunset
```

Optional clock-after-Saturday cycle:

```text
Countdown to Friday sunset
  -> Countdown to Saturday sunset
  -> Clock until Friday
  -> Countdown to Friday sunset
```

Friday mode owns the rules for choosing phase and target. The display manager owns installing and replacing states.

## Blink

Blink is a rendering attribute, not a mode.

```cpp
state.blink = true;
```

The renderer can implement blink by periodically alternating between:

```text
render payload
blank display
```

or, for clock colon blinking, by passing a blink flag to the clock formatter.

## Future Scroll Support

Scrolling can be added later as renderer-owned behavior for `Message` states.

It is intentionally not part of the current `DisplayState` implementation because the current renderer only supports fixed three-panel messages. Keeping it out avoids carrying an unused state flag.

## Why No Overlay Mode

Overlay modes blur responsibilities:

```text
Mode decides rendering.
Overlay interrupts rendering.
Overlay decides when to leave.
Overlay often needs to remember previous mode.
```

The state design makes this explicit:

```text
Temporary behavior is just a state with an expiration.
Returning to prior behavior is a manager transition.
Blinking is a state attribute.
Messages are states.
```

This keeps the system easier to reason about as Friday mode, demo, info messages, and final countdown messages grow.

## Summary

Use:

```text
DisplayBehavior for render type
DisplayState for render data and attributes
DisplayTransition for expiration
DisplayManager for lifecycle
Mode-specific resolvers for domain rules
```

Do not use:

```text
Overlay modes
State-owned next-state pointers
One-off special branches for temporary display behavior
```
