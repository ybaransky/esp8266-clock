# Trading Mode Design

Status: implemented current design.

## Configuration

Trading mode has a fixed-capacity `TradingSchedule` with two retained
`TradingInterval` slots and an enabled `intervalCount`:

```cpp
struct TradingInterval {
  uint16_t startMinute;
  uint16_t stopMinute;
};

struct TradingSchedule {
  uint8_t intervalCount;
  TradingInterval intervals[2];
};
```

Session 1 is always enabled. Session 2 is optional, but its start and stop
remain persisted while disabled. Enabled sessions must be ordered, must not
overlap, and must have a real gap between them. Sessions cannot cross midnight.

JSON stores `intervalCount` and both interval objects under
`display.modes.trading`. Times use `HH:MM` local wall-clock strings at the JSON
boundary and minutes after midnight internally.

## Pure scheduling

`schedule.h/cpp` owns `isValidTradingSchedule()` and
`evaluateTradingSchedule()`. The evaluator walks enabled sessions in order:

```text
before start 1   -> countdown to start 1
inside session 1 -> countdown to stop 1
between sessions -> countdown to start 2
inside session 2 -> countdown to stop 2
after final stop -> countdown to session 1 on the next weekday
```

Saturday and Sunday target Monday session 1. Holidays and early closes are not
modeled. The pure scheduler contains no Arduino, RTC, display, logging, or
storage operations.

## Controller and announcements

`ScheduledModeController` applies the pure `ScheduleDecision` on accepted RTC
samples. Its next boundary has a kind (`kTradingOpen` or `kTradingClose`), local
wall-clock time, and session index. The display always counts down to that
boundary, with the optional over-24-hour format resolved during rendering.

Every live open/close can show its message and play its song. Only the first
open and last close receive the generated approach patterns. Boot, settings
changes, time synchronization, and RTC discontinuities rebase silently. The
shared tracker allows one crossing at most five seconds late and suppresses
multi-boundary gaps. See [scheduling-review.md](scheduling-review.md) for the
common policy and device validation notes.
