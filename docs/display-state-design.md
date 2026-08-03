# Display State Design

Status: implemented current design.

## Three separate concepts

- **Mode** is the persisted user selection in `ClockConfig.activeMode`.
- **View** is the current normal content: clock, countdown, or countup.
- **Overlay** is temporary content drawn above the view: splash, message,
  countdown-complete message, demo phase, or paged information.

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

`OverlayState` contains:

- `overlay`
- copied message text
- copied paged-message data
- expiration policy

## Base-view updates

`DisplayManager::setView()` replaces the base view. Friday and Trading
controllers call it as schedule phases change. If an overlay is active, it
continues rendering while the base view updates underneath.

There is no saved "previous display state". Clearing an overlay renders the
current base view. This prevents a splash or message from restoring a stale
scheduled view.

`applySettings()` is reserved for a full configuration reload. It rebuilds the
mode-derived view and resets presentation cadence; it is not a phase-transition
operation.

## Rendering and timing

- `display_format` declares clock/counting layouts and derives refresh/colon
  behavior from panel shapes.
- `display_renderer` builds demo, message, and page frames without I/O.
- `DisplayManager` owns overlay lifecycle, format selection, blink/colon state,
  and render deadlines.
- `SegmentDisplay` performs TM1637 hardware writes and skips unchanged panels.
- Tenths are derived from `RtcService::msIntoSecond(nowMs)` and remain
  phase-locked to the accepted RTC SQW edge.
