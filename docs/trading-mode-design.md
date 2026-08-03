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
`evaluateTradingBoundary()`. The evaluator walks enabled sessions in order:

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

## Controller

`TradingModeController` snapshots the Trading configuration and open/close
messages. `ClockController` ticks it on every accepted RTC SQW second.

The controller converts the next boundary into a countdown `ViewState` and
installs it through `DisplayManager::setView()`. `TradingPhase::kToOpen` and
`kToClose` describe the boundary type; the target timestamp distinguishes
session 1, session 2, and future weekdays.

## Live announcements

A live `kToOpen -> kToClose` crossing blinks `messages.tradingOpen`. A live
`kToClose -> kToOpen` crossing blinks `messages.tradingClose`. This applies to
both configured sessions.

Applying configuration, booting, or synchronizing the RTC resets the remembered
phase to `kNone`. A transition from `kNone` initializes the view without
announcing a boundary.
