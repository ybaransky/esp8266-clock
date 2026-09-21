# Display State Design

Status: implemented current design.

## Three separate concepts

- **Mode** is the persisted user selection in `ClockConfig.activeMode`.
- **View** is the current normal content: clock, countdown, or countup.
- **Overlay** is temporary content drawn above the view: splash, message,
  hardware fault, demo phase, or paged information.

Rendering always chooses the overlay when one is active and otherwise chooses
the base view.

## State representation

`ViewState` and `OverlayState` are plain structs. They are intentionally not
unions; fields unused by the selected enum value are ignored.

`ViewState` contains:

- `view`
- `anchor` for countdown/countup
- `formatIndex`
- `longFormatIndex`
- `blink` (absolute before/after window)

`OverlayState` contains:

- `overlay`
- copied message text
- copied paged-message data
- expiration policy

## Base-view updates

`DisplayManager::setView()` replaces the base view. The shared scheduled-mode
controller calls it when the current schedule decision changes. If an overlay is active, it
continues rendering while the base view updates underneath.

There is no saved "previous display state". Clearing an overlay renders the
current base view. This prevents a splash or message from restoring a stale
scheduled view.

`applySettings(config, initialView)` is reserved for a full configuration
reload. The application resolves the complete initial view before calling it;
it installs that view and resets presentation cadence.

## Rendering and timing

- `display_format` declares clock/counting layouts and derives refresh/colon
  behavior from panel shapes.
- `display_renderer` builds demo, message, and page frames without I/O.
- `DisplayManager` owns overlay lifecycle, format selection, blink/colon state,
  and render deadlines.
- `SegmentDisplay` performs TM1637 hardware writes and skips unchanged panels.
- Tenths are derived from `RtcService::msIntoSecond(nowMs)` and remain
  phase-locked to the accepted RTC SQW edge.

## Completion and fault priority

`ClockController` detects ordinary countdown completion on RTC samples even
under an overlay. It tells the display to render the final base message, clears
ordinary information, and plays the cue once. Scheduled countdowns cannot
produce that completion state. Boot/config/time sync arrival is silent.

A hardware fault takes priority over ordinary overlays and completion.
`clearFault()` reveals current base content, including a completed countdown.
Expiration also returns to that content; there is no previous-view snapshot.
