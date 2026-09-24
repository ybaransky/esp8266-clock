# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

You are a senior software engineer with 15+ years of experience. When providing code solutions, follow these principles:

## DESIGN PRINCIPLES
- Apply SOLID principles, with one deliberate exception recorded below for Dependency Inversion.
- **Constructor injection of concrete types**, not interfaces. Each collaborator has exactly one implementation and a vtable costs flash plus an indirect call on a render path that runs 10x/second, so the objects are injected but not abstracted. The logic actually worth substituting is already pure (`schedule.cpp`, the format renderers) and is tested directly on the host. The one interface that earns its keep is `RebootScheduler`, which exists to break an ownership cycle: `WebPortal` constructs the API handlers, so they must not depend back on `WebPortal`.
- Objects own their state. A class that is really a façade over file-static globals is not injectable no matter how it is passed around; `RtcService` and `SegmentDisplay` hold their hardware and cache as members for this reason.
- Minimize coupling between classes/modules.
- Favor composition over inheritance.
- Use clear abstractions and interfaces to separate concerns.
- Within a file, prefer classes (including singletons). Across files, prefer functions for module boundaries.

## CODE READABILITY
- Write self-documenting code with meaningful names that reveal intent.
- Keep functions small and focused (do one thing).
- Avoid deep nesting. Use early returns and guard clauses.
- Add concise comments only where the "why" is not obvious.
- Follow Google coding conventions when applicable: `PascalCase` types,
  `camelCase` functions and locals, trailing underscores for private members,
  and `kPascalCase` constants and enum values. Reserve `ALL_CAPS` for macros
  and established hardware pin identifiers.
- In compound `if` conditions, parenthesize each comparison explicitly —
  `if ((a == b) && (c < d))` — never rely on operator precedence.
- Every class/struct declaration is preceded by a comment stating what it is
  responsible for and how it fulfills that responsibility.
- Every member variable carries a brief same-line comment describing its role.

## ARCHITECTURE
- Separate concerns into distinct layers (data, logic, presentation).
- Define clear boundaries between modules.
- Avoid leaky abstractions.
- Prefer explicit behavior over implicit behavior.

## OUTPUT FORMAT
- Before writing code, briefly explain design decisions and tradeoffs.
- After code changes, note further improvements worth considering.
- If the task is large, outline the structure first and confirm before implementing.

## BUILD COMMANDS

```bash
# Build firmware
pio run

# Build and upload to device
pio run --target upload

# Upload filesystem (LittleFS data/ directory) to device
pio run --target uploadfs

# Monitor serial output (74880 baud with ESP8266 exception decoder)
pio device monitor

# Edit web pages against a live device without reflashing: serves web/
# sources raw at localhost:8080 and proxies /api/* to the device
python tools/dev_server.py --device <clock-ip>

# Inspect the packed sound catalog (also rebuilt by any pio run target)
python tools/pack_songs.py list
python tools/pack_songs.py dump "Pacman Intro Theme"

# Clean build artifacts
pio run --target clean

# Host unit tests for the pure modules (schedule, display formats, datetime).
# Must run where a host C++ compiler exists. On the author's Windows box that
# means WSL: Smart App Control is enforced and blocks unsigned executables, so
# MinGW's cc1.exe/cc1plus.exe cannot launch. PLATFORMIO_BUILD_DIR is required -
# see "Sharing .pio between two PlatformIO installs" below.
PLATFORMIO_BUILD_DIR=$HOME/.cache/pio-build/esp8266-clock pio test -e native
# Or in VS Code: Ctrl+Shift+P -> Tasks: Run Test Task (.vscode/tasks.json)

# Format-catalog invariants: key uniqueness, defaults resolvable, overflow
# fallbacks present. Runs automatically as a pre-script on every `pio run`.
python tools/check_formats.py
```

Validation is primarily by flashing the firmware and observing behavior on device. Serial output at 74880 baud includes stack traces decoded by `monitor_filters = esp8266_exception_decoder`.

Two automated layers back that up:
- **`tools/check_formats.py`** runs on every build and fails it on a duplicate format key, a default naming a nonexistent format, or a combined `hhh:mm` format with no split fallback. It parses the C++ rather than compiling it, so it needs only Python.
- **`test/` holds Unity suites** for `schedule.cpp`, `display_format.cpp`, and `datetime_validation.cpp` - the modules that are pure by design. They run via `pio test -e native` wherever a host compiler exists; `test/stubs/` supplies host stand-ins for `Arduino.h` and RTClib's `DateTime`.

**Sharing `.pio` between two PlatformIO installs.** Firmware builds run under
Windows and host tests run under WSL, so two PlatformIO cores operate on the
same project. PlatformIO keeps a single `project.checksum` for the whole build
directory and **wipes that directory when the checksum does not match** - and the
two installs compute different checksums. Left alone they delete each other's
output on every switch, which shows up as a full firmware rebuild on every
`pio run` plus a C/C++ extension warning that `.pio/build/d1_mini` cannot be
found (`c_cpp_properties.json` points at the directory the test run just
removed). Always give the WSL side its own build directory via
`PLATFORMIO_BUILD_DIR`, as the command above and `.vscode/tasks.json` do.
Putting it on the Linux filesystem rather than under `/mnt/c` is also about 35%
faster.

**The host test build is `-Werror`.** A warning there fails the run outright,
deliberately only on `[env:native]`: its compile output scrolls past above the
test results and an incremental run recompiles nothing, so a warning is easy to
miss entirely - and it is the build most likely to catch a real portability
defect, since `long` is 64 bits on the host and 32 on the ESP8266 (that is how
the trading-boundary arithmetic bug surfaced). If a toolchain upgrade one day
fails the tests on a new warning, read it and fix it or drop the flag; do not
reach for `-Wno-<whatever>` without understanding what it hides.

The firmware build is warning-clean with `-Wall -Wextra` but **not** `-Werror`,
because its `build_flags` also reach the Arduino core and five third-party
libraries. Gating only our own sources there would need `build_src_flags`. `-Wno-deprecated-copy` is applied to C++ only (via `tools/cxx_flags.py`) because RTClib declares `DateTime`'s copy constructor without its assignment operator, and eleven instances of that third-party defect would drown our own warnings.

## ESP8266 CLOCK PROJECT CONVENTIONS

### Hardware pin map (authoritative source: `src/hardware.h`)

| Signal        | Pin | GPIO   | Notes                                                              |
|---------------|-----|--------|--------------------------------------------------------------------|
| *Left side*   |     |        |                                                                    |
| TM1637 DIO[2] | D0  | GPIO16 | No interrupts; fine for DIO                                        |
| TM1637 CLK    | D5  | GPIO14 | Shared across all 3 displays                                       |
| TM1637 DIO[1] | D6  | GPIO12 | Safe                                                               |
| DS3231 SQW    | D7  | GPIO13 | RISING interrupt, INPUT_PULLUP, 1Hz                                |
| Buzzer        | D8  | GPIO15 | **Strapping pin; must stay LOW at boot.** Inverting NPN buffer required — see `WIRING.md` |
| *Right side*  |     |        |                                                                    |
| DS3231 SCL    | D1  | GPIO5  | Hardware I2C                                                       |
| DS3231 SDA    | D2  | GPIO4  | Hardware I2C                                                       |
| Button        | D3  | GPIO0  | INPUT_PULLUP, pressed = LOW; do not hold at boot                   |
| TM1637 DIO[0] | D4  | GPIO2  | Shares with INTERNAL_LED; both idle HIGH                           |
| Internal LED  | D4  | GPIO2  | Active-low; shared with TM1637 DIO[0] — LED flickers during write |

### Pin boot constraints
- **GPIO15 (D8)**: must be LOW - the buzzer's 10k base pulldown is what satisfies this. Any pull-up prevents boot/flash, which is why the active-LOW buzzer module (S idles at 2.7V) cannot sit on D8 directly. `WIRING.md` explains the NPN buffer and why a plain pulldown does not work.
- **GPIO0 (D3)**: must be HIGH - INPUT_PULLUP + button not pressed.
- **GPIO2 (D4)**: must be HIGH - LED and TM1637 DIO both idle HIGH; safe.

### Serial / I2C / RTC time
- Serial at 74880 baud for readable ESP8266 boot output.
- Initialize I2C early in `setup()` with explicit SDA/SCL pins before probing the RTC.
- `ClockApplication` owns `RtcService` from `rtc_ds3231.h` and injects it into the controller, display manager, and config API. **All of its state is in members**; the only file-static state is the three `volatile` counters the SQW ISR writes, which are genuinely singleton (one SQW pin) and kept out of the object so the `IRAM_ATTR` handler does not chase a pointer.
  - `RtcStatus` is a trivially-copyable POD with a fixed `char error[48]`, not a `String`. `getStatus()` is used as a cheap predicate (`isHealthy()`, the 2s health poll), and a `String` member made every one of those calls allocate and free on the heap.
  - `begin()`, `getStatus()`, `getNow()` (live I2C read), and `setNow()` (also resyncs the cache) provide device operations.
  - SQW processing uses `beginSqwProcessing()` and `consumeSqwPulse(RtcTick&)`. The returned sample carries local time, its ISR phase reference, and a discontinuity flag.
  - RTC servicing owns the :00/:30 live-read resync, before schedules use the sample. Logging only prints at second zero. Pending count and latest edge are consumed together; backlogs, recovery, and corrected time jumps suppress past announcements. Stale queued edges wait for a fresh pulse; an edge during I2C defers resync to the next loop.
  - `getNowCached()` provides second-resolution time at zero I2C cost and is required on the display-render path.
  - `isHealthy()`: RTC present and SQW pulse arriving on schedule. Drives the "no rtc" overlay in `clock_application.cpp`.
  - `msIntoSecond(nowMs)` is clamped to 0-999 and phase-locked to the ISR timestamp. Never compute tenths from `millis() % 1000` directly.
- For fatal exception debugging, keep exception decoding enabled and include decoded stack traces in reports.

### Logging
- `log.h` provides `LOG_PRINTLN(msg)` and `LOG_PRINTF(fmt, ...)` macros.
- **A log line costs no I2C transaction and no heap allocation.** The timestamp comes from `RtcService`'s software cache via a static provider that never reads the chip; before the cache is seeded the logger prints `--:--:--` rather than falling back to a bus read. `RtcService::begin()` seeds the cache from the read it already performs, so boot lines still carry a real time. This matters because logging is the most frequent time consumer in the system, and it sits inside web handlers and the SQW consume path - the whole reason `getNowCached()` exists.
- Each line is prefixed with `logCurrentTime()`, the peak cont-stack usage in bytes (bare value, of 4096), and `logSourceName(__FILE__):__LINE__`.
- Both macros keep their strings in **flash** (`PSTR` + `Serial.printf_P`). On the ESP8266 a plain string literal occupies RAM for the life of the program; moving the log strings to flash is what holds static RAM under 50% (OTA headroom). Consequences:
  - `LOG_PRINTLN(msg)` and the `fmt` of `LOG_PRINTF` **must be string literals**. For a runtime string, use `LOG_PRINTF("%s", value)`.
  - `LOG_PRINTLN` pastes its literal into the printf format, so a literal `%` must be written `%%`.
- Each macro call emits exactly one line and appends the terminating newline itself, so **formats must not end with `\n`**.

### Display / mode architecture
The display system is layered as follows:

1. **`display_format.h/cpp`** - the clock/counting format catalog and pure renderers (no I/O).
   - `config.h` owns the persisted `Mode` enum. `display_format.h` owns `FormatGroup`, `DisplayFormatInfo`, `displayFormatCount()`, `displayFormatInfo()`, `displayFormatKey()`, `displayFormatLabel()`, `displayFormatIndexForKey()`, `renderCountingFormat()`, and `renderClockFormat()`.
   - **A format's identity is its key, not its table position.** Each row has a stable `key` (e.g. `"yyyy-mmdd-hhmm"`) that is what `/config.json` stores and what the web dropdowns post; rows may be added, removed, or reordered freely. An index is an in-memory detail only, converted at the serialization boundary. This is the same rule sound names follow, and it exists because the old index-valued scheme had already produced three disagreeing claims about the default clock format. `tools/check_formats.py` enforces key uniqueness and default resolvability on every build.
   - Keys and labels live in **parallel PROGMEM tables** (`kCountingKeys`/`kCountingLabels`/`kClockKeys`/`kClockLabels`), indexed identically to the spec table; `static_assert`s hold the three in lockstep. They are read by copying into a caller buffer (`kFormatKeyLength` / `kFormatLabelLength`) because a `const char*` member would only put the pointer in flash, not the text - 1.2KB of DRAM at stake. `displayFormatIndexForKey()` compares with `strncmp_P` and needs no buffer.
   - The **single source of truth** for rendering is in `display_format.cpp`: each `FormatSpec` is `{PanelSpec panels[3]}`, where a `PanelSpec` is a declarative `{Shape, Field a, Field b}` triple (e.g. `kColon` + `kHours`/`kMinutes`). The label is human-readable only; the panel shapes are the only source of truth for rendering. `RefreshRate` and `ColonAnimation` are **derived** from the panel shapes (`kColonTenths` / `kColonBlink`), so a row's scheduling metadata can never drift from what it renders.
   - Countdown and CountUp share `kCountingFormats`; the two modes cannot drift apart.
   - `renderCountingFormat()` and `renderClockFormat()` return a complete `DisplayFrame` by interpreting the panel shapes in `renderPanels()`.
   - Label tokens: counting uses `dd`/`hh`/`mm`/`ss`/`u` (tenths) and `hhh` (total hours = days*24+hours); clock uses `YYYY`/`MM`/`DD`/`DOW`/`hh`/`mm`/`ss`/`u`. `H` and `N` are labels rendered as lowercase `h`/`n`; `DOW` renders Sun/non/tu/uEd/thu/Fri/Sat (7-segment-safe forms).
   - A semicolon in a clock label (`hh;mm`) marks a blinking colon; its panel uses `Shape::kColonBlink`, which is what makes the format report `ColonAnimation::kBlinking`. Fixed or absent colons report `ColonAnimation::kNone` because they require no animation scheduling.
   - `hhh:mm` combined on one panel only works through 99:59; above 99 hours `resolveCountingOverflow()` semantically selects the matching split `hhh | mm` variant (same seconds panel) - no hardcoded indices.
   - Counting formats hide leading zero panels via `suppressLeadingZeroPanels()`: panel 0 blanks when zero, panel 1 only when panel 0 is already blank; the last panel always renders.
   - Numeric-only panels are right-justified across the four characters (`7` renders as `"   7"`). For colon formats, the value left of the colon is blank-padded, not zero-padded (` 9:05`). When a blinking colon is off, the time renders without a separator (` 905`) so all digits remain visible.

2. **`display_frame.h`** - `DisplayFrame` plus the panel-count constants, and nothing else: no driver, no Arduino dependency. Split out of `display.h` so the pure renderers (and their host tests) can build frames without the TM1637 library.

2b. **`display.h/cpp`** - `ClockApplication` owns `SegmentDisplay`, which owns 3 `TM1637Display` objects **as members** (brace-initialized with their pin pairs) and is attached to `DisplayManager` during startup.
   - `ASCII_SEGMENTS` is `PROGMEM`, read through `pgm_read_byte()`; 96 bytes of DRAM for one flash read per rendered character.
   - `begin(brightness)`, `setBrightness(0-7)`, `showFrame(frame)` (takes the 3-panel `DisplayFrame` from `display.h`), `blank()`.
   - Panel strings use `:` or `;` between the second and third visible slots as non-consuming markup for the panel's center colon. This hardware has no decimal points; `.` has no special rendering behavior.
   - Caches last-written segments per panel; skips hardware write on identical content. At each wall-clock `:00:00`, `ClockController` asks `DisplayManager::notifySecondBoundary(true)` to invalidate this cache; the normal render in the same loop then resends all four digits on all three panels, correcting hardware state that drifted without visibly blanking the display.
   - ASCII-to-segment glyph mapping lives in `display.cpp` as `ASCII_SEGMENTS`; adjust that table when a letter does not display well on 7-segment hardware.

3. **`display_manager.h/cpp`** - application-owned `DisplayManager`; the single entry point for all display state.
   - The model: the persisted **Mode** resolves to a base **View** (`View::kClock/kCountdown/kCountup` - what content is currently rendered), optionally covered by a temporary **Overlay**. Explicit overlays distinguish splash, blinking message, hardware fault, demo countdown/final message, and pages. Ordinary countdown completion is base presentation controlled by `ClockController`, independent of overlays.
   - `ViewState` is a plain struct: `{view, anchor, formatIndex, longFormatIndex, blink}`. `anchor` is the countdown end time or countup start time; unused for clock. No unions. `longFormatIndex` (default `kSameFormat` = disabled) selects an alternate counting format while the remaining/elapsed duration is >= 24h; `activeCountingFormatIndex()` resolves it fresh on every render (and for the refresh cadence), so the display reverts to `formatIndex` on its own when the duration drops below 24h - no crossing state is kept.
   - `OverlayState` is a plain struct: `{overlay, message[64], paged, transition}`. Behavior is explicit in the `Overlay` value instead of boolean flag combinations; unused fields are ignored.
   - `applySettings(config, initialView)` (hot-reload; the application resolves the view before rendering), `tick(nowMs)`, and `setBrightness()`.
   - `setView(state)` replaces the base view. If an overlay is active, the new view simply becomes visible when the overlay clears - the view keeps updating live underneath; there is no snapshot to keep in sync. Used by `ScheduledModeController` when its decision changes.
   - Overlays: `showSplash(msg)`, `showDemo()`, `showInfo(msg, durationMs = kForever)`, `showPages(pages, count, ...)`. `showFault()` has priority over ordinary overlays; only `clearFault()` or a new fault can replace it. Expiration reveals the current base view.
   - `ViewState.blink` is a `BlinkWindow` - a half-open `[fromUnix, untilUnix)` range of local wall-clock seconds during which the **whole base view** blinks at `kViewBlinkMs` (250ms half-period = 2 blinks/second). An empty window (the default) disables it. `render()` advances the blink phase before the frame builders consult the render throttle - a toggle forces its own frame regardless of the format's refresh rate - and blanks the finished frame on the "off" phase. Membership is re-tested every render (`viewBlinkActive()`), so like `longFormatIndex` the window ends on its own and needs no crossing state; a window that closes on an "off" phase forces one immediate visible frame instead of waiting for the next refresh. Absolute times (not a duration relative to `anchor`) are what let one field express both "before a boundary" and "after a boundary". Used by Friday mode's sunset blink windows.
   - Overlay frames come from **`display_renderer.h/cpp`**: pure functions (`renderBlankDisplayFrame`, `renderDemoDisplayFrame`, `renderMessageDisplayFrame`, `renderPageDisplayFrame`) that convert explicit data into a `DisplayFrame` with no I/O or scheduling.
   - `ClockController::activeMode()` is the persisted mode; `DisplayManager::activeView()` is the base view (never an overlay). Friday and Trading modes update their views over time.
   - Clock colon blink toggles once per second (2-second full cycle); message/page blinking uses its own 500ms cadence. `DisplayFormatInfo::refreshRate` selects 100ms for tenths formats and 1s for the others.
   - Tenths values come from the injected RTC service's `msIntoSecond(nowMs)`, not `millis() % 1000`. `notifySecondBoundary()` invalidates the render throttle on each accepted SQW pulse. Demo tenths remain deadline-derived.
   - When `ClockConfig.display.clockUse12Hour` is true, hours are converted to the 1-12 scale locally in the clock renderer only; countdown/countup are unaffected.

4. **`clock_controller.h/cpp`** - owns mode resolution, ordinary countdown completion, and the shared `ScheduledModeController`. It does **not** tunnel sound previews or catalog queries: six pass-throughs with no logic of their own had turned it into a service locator for the web handlers, so `ConfigApi` holds `SoundPlayer&` directly. It resolves the complete initial view before applying display settings. Count-up's origin is read from `countup.start`, which is always an absolute datetime by the time it gets here - `resolveCountupStart()` stamps the `kCountupStartNow` sentinel at the API layer on the way to disk. The sentinel branch in `initialView()` therefore only fires on a device that has never saved, and the anchor is retained in the display's base view across time syncs. On each `RtcTick`, it advances the active schedule and ordinary countdown independently of overlays; discontinuities silently rebase both. Expired countdowns on config apply/time sync show the final message without a cue. Live completion replaces ordinary information but preserves a hardware fault. Display rendering never detects or announces completion.
5. **`time_api.h/cpp`** - owns `GET /api/time` and `POST /api/time`; reads through `RtcService` and synchronizes through `ClockController`.

### Scheduled modes

- **`schedule.h/cpp`** owns pure `evaluateFridaySchedule()` and `evaluateTradingSchedule()` calculations. Both return `ScheduleDecision`: a clock/countdown view and a named next boundary (`kind`, `atLocalSeconds`, Trading `sessionIndex`). Exact boundaries belong to the following interval; targets are strictly future for valid inputs. Times are local wall-clock seconds, not UTC instants.
- **`scheduled_mode.h/cpp`** owns one `ScheduledModeController`, including the shared previous-decision/time tracker, weekly sunset cache, view mapping, message/song pairing, and approach-alert selection. It is ticked on accepted RTC samples, never on a logging interval.
- **`tick()` refreshes the sunset cache once, up front; `evaluate()` is `const` and pure with respect to it.** Previously `evaluate()` wrote the cache, so `crossedBoundary()` - a predicate - moved it as a side effect, and correctness depended on a documented statement order. `refreshSunsets()` is the only writer and returns early outside Friday mode.
- **`viewFor()` dispatches to `fridayViewFor()` or `tradingViewFor()`.** The combined version reached for `friday_.clockFmt` on any clock decision, which was correct only because `evaluateTradingSchedule()` happens never to return one; the split makes that combination unrepresentable rather than latently wrong. `test_trading_always_counts_down` pins the underlying property down.
- Two scheduled modes are deliberately **not** abstracted behind a strategy table. With exactly two, four function pointers and three context structs would add more machinery than the `mode_` branches cost. Revisit when a third mode actually exists.
- A live crossing requires `previousTime < previousBoundary <= now`, is at most five seconds late, and must not have skipped another boundary. Boot, config apply, browser time sync, backward jumps, and RTC discontinuities install state silently. `reset()` invalidates both remembered state and the sunset cache. `start(now)` seeds the initial decision without announcing it.
- Friday displays clock from Saturday sunset to Friday midnight, countdown to Friday sunset, then countdown to Saturday sunset. Friday midnight is a silent boundary. Only Friday sunset has a message/song; both sunsets have their respective generated approach alerts.
- Sunset uses `calculateSunset()` with the physical device location and numeric UTC offset, cached for the most recent Friday. The 18:00 local anchor selects the correct UTC calculation date; invalid coordinates or NaN fall back to local 18:00.
- Friday blink windows still bracket Friday sunset, resolve at render time, and add no schedule phases. The post-sunset window continues beneath the five-second sunset message.
- Trading retains two configured same-day sessions plus an enabled count. `isValidTradingSchedule()` requires ordered, separated, non-overlapping enabled sessions. It counts down through weekday opens/closes, then the next weekday's first open; holidays and early closes are not modeled.
- Every Trading open/close can announce its message/song. Only the first open and last close have generated approach alerts. A newly eligible approach alert takes precedence over a boundary song when their windows overlap.
- Trading's optional over-24-hour format remains a render-time decision in `ViewState.longFormatIndex`.

### Sound
- **`sound_player.h/cpp`**: application-owned `SoundPlayer`, injected into `ClockController` and supplied explicitly to `ScheduledModeController`.
- **Playback is a non-blocking deadline state machine**, not a port of `esp8266-sound`'s player. `tick(nowMs)` advances at most one note boundary and returns; the tone itself is produced by the ESP8266 waveform generator (`analogWriteFreq` + `analogWrite`) and keeps sounding without software help. This is the whole reason the source project's design was not reused: its `cooperativeDelay` loop blocks for the length of the song and re-enters its caller through a service callback, which here would recursively consume SQW pulses and re-enter `DisplayManager` and the web handlers. **Never add a blocking delay to the playback path.**
  - The generated boundary approach alert shares this player and therefore replaces any sound already playing. It has four equal phases; each boundary configures its starting beep rate and that rate doubles for every subsequent phase. A beep is `min(100 ms, half the pulse period)` and occupies the end of its pulse slot, so every beep remains distinct and the final one ends at the boundary. Playback is derived from absolute elapsed time each loop, so a stall skips expired pulses instead of replaying a backlog.
  - `sound.boundaryAlert` stores `enabled` plus independent `boundary1` and `boundary2` objects. Each object holds `toneHz` (100-5000), `totalDurationSeconds` (4-1200), and `startingBeatsHz` (1-10). The player divides total duration evenly across the four phases. Defaults are 880 Hz/40 s/2 Hz for Boundary 1 and 1320 Hz/40 s/2 Hz for Boundary 2. The regular sound master switch also gates them. Boundary 1 announces Friday sunset and the first Trading session open; Boundary 2 announces Saturday sunset and the last Trading session close. Intermediate Trading session boundaries retain their existing messages/catalog songs but do not receive the generated day-boundary alert.
  - Scheduled-mode controllers update the armed wall-clock target every real SQW second. `ClockController` passes the SQW-derived monotonic start of that second, so boot/config-save arrival inside the window resumes at the correct pattern position and still finishes on time. Mode changes and browser time synchronization cancel the armed target.
  - Deadlines advance from the *previous* deadline, not from `millis()`, so per-note rounding cannot drift. A stall longer than `kMaxCatchUpMs` (250ms) re-anchors to the current time instead of racing through the backlog.
  - Volume is the PWM duty cycle (50% = loudest), so `setVolume()` is heard within the current note. Setting `USE_SOFTWARE_VOLUME` to 0 reverts to full-volume `tone()` for use with the hardware potentiometer in `WIRING.md`; the two must not stack.
  - `analogWriteFreq()` clamps below 100 Hz. The catalog's single 82 Hz pitch is therefore slightly sharp; a piezo reproduces almost nothing down there, so this is left alone.
- **Every catalog entry has a `SoundKind`: `kAlert` (short burst) or `kSong` (full melody).** The kind is set by **where the source lives** - `assets/sounds.json` holds alerts, `assets/songs/*.json` holds songs - so there is no `kind` field to author and moving a file reclassifies it. Both kinds share one catalog, one binary layout, and one player; they differ only in which dropdown offers them.
  - Stored as a `u8` per **directory entry**, not as an ordering: the directory stays name-sorted across both kinds, so adding one alert does not move every song. `SoundPlayer::namesAsJson(array, kind)` filters during the directory walk it already performs. `play()` takes a name and ignores kind.
  - An unrecognized kind byte reads as `kSong`, so a kind added later still appears somewhere rather than vanishing from the UI.
- **`assets/sounds.json` + `assets/songs/*.json` → `data/songs.bin`** via `tools/pack_songs.py` (a PlatformIO pre-script). The sources live in `assets/` so they never reach the device; the `.bin` is gitignored and regenerated when a source is newer. **The binary layout is spelled out in exactly two places - `tools/pack_songs.py` and `sound_player.cpp` - and must change together.** See `SOUNDS.md` for the format and the workflow.
  - 22 songs + 6 alerts, 3,555 notes, 50 distinct pitches in 7,909 bytes (one LittleFS block). Only the pitch table is cached in RAM; notes stream two bytes at a time from an open file handle, so song length costs no memory.
  - **The packer counts itself among its own sources**, so a layout change rebuilds `songs.bin` with no song JSON edited. Without that, a version bump would leave the device holding an image the new firmware refuses to read - which is what a version mismatch logs, naming `pio run -t uploadfs`.
  - The packer reads its own output back and compares every note before writing, and rejects a name longer than `kSoundNameLength - 1` at build time rather than letting it be truncated into one that matches nothing.
  - Only the `Import("env")` probe at the bottom of the packer sits inside `except NameError`. Widening that try to cover the build would report any `NameError` in the script as "not running under PlatformIO" - note that SCons does **not** define `__file__` for a pre-script, so paths there come from `$PROJECT_DIR`.
- **A sound's name is its identity** - in the catalog, in `/config.json`, and in the API. `kSoundNameLength` (48, in `config.h`) bounds all three and must match `MAX_NAME_BYTES` in the packer. Names are deliberately *not* validated against the catalog on save: the filesystem image can be re-uploaded independently of the config, so a name that matches nothing today may match tomorrow. `SoundPlayer` treats a miss as silence and logs it.
- **Announced events are named live boundaries**. Friday sunset, Trading open, Trading close, countdown-reaches-zero, and startup each pair a message with a sound.
  - `applySettings()` copies each cue name through `activeSoundName(config.sound, name)`, which returns `""` when `sound.enabled` is false. **The master switch is resolved once, at config time**: an empty name already means silence everywhere, so no tick-path or render-path code tests the flag.
  - Messages and songs share the same boundary eligibility check. Reset/rebase operations never announce elapsed boundaries.
  - Ordinary countdown completion is owned by `ClockController` and detected on accepted RTC samples even under an overlay. It calls `setCountdownComplete()` and plays its cue once; rendering has no sound or completion-event dependency.
- `POST /api/sound/test` **bypasses** the master switch: pressing preview is an explicit request to hear something, and staying silent would read as a broken button rather than as a setting.
- `play()` always replaces whatever is sounding, and an empty name stops playback. A long song therefore plays on under the base view after its 5s message overlay clears, and the next boundary cuts it off.
- Playback logging: `play()` logs `sound: playing <name> (<n> notes)` after the first note actually starts, so a truncated record never claims playback. The empty-name path stays silent - every disabled cue takes it on every boundary. `stop()` logs in both branches (`sound: stopped <name>` / `sound: stop requested; nothing playing`) because it is only reachable from the `/sound` and `/format` stop buttons; internal stops go through `endPlayback()`, which is silent.
- **`GET /api/sounds` returns `{songs: [], alerts: [], available}`** - two arrays rather than one list with a kind per entry, since every consumer fills one dropdown or the other. `available` false means no readable `/songs.bin` at all, which the pages say out loud instead of showing an empty dropdown that looks like a bug.
- `/sound` is the sound page: it auditions any song or alert (play + stop, nothing saved) and owns the three settings with no per-mode home - master enable, volume, and the startup song. `/format` assigns a **song** to each event cue (countdown zero, Friday sunset, Trading open/close) and posts only those names; patch semantics leave `/sound`'s three fields untouched. Alert cues are not wired to events yet.

### Input
- **`button.h/cpp`**: `buttonBegin()`, `buttonTick()` (debounce), `buttonHasEvent()`, `buttonNextEvent()`.
  - `ButtonEvent` enum class: `kNone`, `kShowSsid`, `kShowIpAddress`, `kShowRtcStatus`.
- **`page_manager.h/cpp`**: `ClockApplication` owns `PageManager` and injects its `DisplayManager` dependency. `showSsid(ssid)` and `showIpAddress(ip)` build `DisplayPage` arrays and hand them to `showPages()`.

### Geography
- **`zipcode.h/cpp`**: `zipcodeLookupLocation(zipcode, &out, path)` - reads `/zipcodes.bin` on LittleFS; `isValidZipcode(zipcode)`. This table is not very accurate unfortunatly, but close enough.
  - `/zipcodes.bin` (164 KB, 33,100 records) is **generated** by `tools/build_zipcodes.py` (a PlatformIO pre-script) from `assets/zipcodes.csv`, which is the editable source and stays out of `data/` so it is not uploaded. The `.bin` is gitignored; any `pio run` target regenerates it when the CSV is newer, and the script verifies its own output by looking every source row back up before writing.
  - **The binary layout is spelled out in exactly two places - `tools/build_zipcodes.py` and `zipcode.cpp` - and must change together.** 8-byte header (`"ZIPB"`, version, record size, `uint16` record count), then a 1001-entry `uint16` directory of cumulative record indexes (one per three-digit prefix plus a terminator), then 5-byte records in ZIP order: `uint8` suffix, `int16` latitude, `int16` longitude. A prefix's records are `[directory[prefix], directory[prefix + 1])`, so one 4-byte read gives both the bucket's position and its length; the prefix itself is implied by the directory slot and never stored.
  - Lookup is 2 seeks and at most 100 sequential 5-byte reads - not a scan of the file. `ZipcodeTable` in `zipcode.cpp` validates the header on open and rejects a truncated upload by comparing `file.size()` against the declared record count.
  - Coordinates are quantized to **0.01 degrees** (~1 km, under 2 seconds of sunset error). Do not expect more precision back than that; `location_api.cpp` logging at `%.6f` is unchanged but the trailing digits are now zeros.
  - Sunset math uses only numeric `utcOffsetMinutes`; the timezone name is persisted for browser/UI context.

### Storage / config
- `ClockConfig` (in `config.h`) holds: `activeMode`; display, counting, Friday, Trading, message, and location groups; and `timezone` with its IANA name and numeric UTC offset.
- **Every config struct member has a default member initializer.** `defaults.cpp` then assigns only what differs from zero, so a field added later is initialized by construction rather than by remembering to add a line - the previous hand-maintained initializer could silently persist indeterminate bytes to `/config.json`.
- **`countup.start` holds an absolute datetime; `kCountupStartNow` ("now") means "never been set", not "start from the present".** The sentinel exists because no absolute datetime is a correct factory default - a shipped date would make a fresh device show an ever-growing elapsed time, which is exactly how the shipped `countdown.end` goes stale. `resolveCountupStart()` substitutes the RTC's current time on the two paths that persist a `ClockConfig` (`POST /api/config` and `POST /api/mode`), after validation so a 400 stamps nothing and an explicit start time in the payload wins. Everything downstream then sees an absolute instant.
  - This is what makes the origin survive **both** later saves and reboots. Before it, the sentinel was resolved in `ClockController::initialView()` on every `applyConfig()`, so saving any unrelated field - a volume, a Trading session time - silently restarted the count-up, and a reboot did too. The sentinel's `initialView()` branch is kept for the never-saved case only.
  - `saveWifiConfig()` deliberately does **not** resolve it: it carries the cached clock config through untouched, and a WiFi change is no reason to stamp a count-up origin.
  - The `/format` page needs no sentinel handling of its own. A blank Start time field posts the sentinel, the device stamps it, and the concrete value comes back on the next load - so the field populates itself. "Use Current Time" remains the way to see and edit the value before saving.
- `display.clockUse12Hour` serializes as `display.clock12Hour` (boolean) in `/config.json`. Default `false` (24-hour).
- `ClockConfig.friday` adds `blinkBeforeMinutes` and `blinkAfterMinutes`, serialized as `display.modes.friday.blinkBeforeMinutes` / `blinkAfterMinutes` and clamped by `sanitizeBlinkMinutes` (0-240; default 0 = off, so existing `/config.json` files are unaffected).
- `ClockConfig.messages` stores `splash`, `final`, `fridaySunset`, `tradingOpen`, and `tradingClose`; they serialize under `display.messages` and are sanitized with `sanitizeDisplayMessage` (max 12 printable ASCII characters). Every buffer in the chain - `MessageConfig`, `OverlayState::message`, `DisplaySettings::finalMessage`, `BoundaryCue::message` - is `kDisplayMessageLength` (`config.h`), the same one-constant-per-concept rule `kSoundNameLength` follows. The buffer is deliberately much larger than the 12-character cap: the shipped messages use leading spaces to position text across the three panels. Trading boundary defaults are `"OPEN"` and `"CLOSE"`.
- `ClockConfig.sound` mirrors `MessageConfig` field-for-field (`startup`, `final`, `fridaySunset`, `tradingOpen`, `tradingClose`) plus `enabled`, `volumePercent`, and the generated `boundaryAlert` settings; it serializes under a top-level `sound` object, not under `display`. Names are sanitized with `sanitizePrintableText` into `kSoundNameLength` buffers. Patch semantics mean an existing `/config.json` with no `sound` object loads unchanged from the defaults.
- `ClockConfig.trading` contains its normal/over-24h formats and a `TradingSchedule`. JSON stores `display.modes.trading.intervalCount` plus both entries in `intervals`, even when only session 1 is enabled, so disabling session 2 does not discard its configured times. Older array-only JSON remains readable: its array length becomes the enabled count.
- `LocationInfo` contains `latitude`, `longitude`, and `zipcode[6]`. `ClockConfig.locations` keeps distinct `device` and `sunsetTest` values. `/config.json` retains separate `location` and `sunset` objects. Do not cross-read one for the other or use one as a fallback for the other.
- `WifiConfig` holds: `staSsid`, `staPassword`, `apSsid`, `apPassword`.
  - **An empty `apSsid` is a sentinel, not a missing value**: it means "derive `ESP_XXXXXX` from the soft-AP MAC at startup". `defaults.cpp` and `data/config.json` both ship it blank, so a fresh filesystem upload advertises the SDK-style name; a user-chosen name saved from `/wifi` persists and wins on the next boot. The derived name is **never written back** to `/config.json` - that is what lets clearing the field return to auto.
  - `sanitizeWifiConfig` enforces only what `softAP()` requires: an AP SSID over 32 characters reverts to the empty sentinel, and a password outside WPA2's 8-63 characters (including empty) reverts to `kDefaultApPassword`. Empty SSIDs are deliberately left alone.
- **`config_serializer.h/cpp` is the only home of the JSON schema, in both directions.** Never spell out config field paths anywhere else.
  - Struct → JSON: `serializeClockConfig(doc, config)`, `serializeWifiConfig(doc, wifi)` (full, for disk), `serializeWifiStatus(doc, wifi)` (no station password, for HTTP responses).
  - JSON → struct: `applyJsonToClockConfig(root, cfg)` and `applyJsonToWifiConfig(root, wifi)` use **patch semantics** (absent fields untouched). Loading `/config.json` (base = defaults) and applying a `POST /api/config` payload (base = loaded config) are the same operation through the same function. `applyJsonToClockConfig` returns `nullptr` or a static error-JSON for the first invalid value; it keeps applying the remaining fields so one bad value can't wipe the rest of the file on load, and API callers discard the partial cfg on error.
- **`ConfigManager` hands out its cache by reference** (`clockConfig()`, `wifiConfig()`), not by value. `ClockConfig` is ~700 bytes and the ESP8266's cont stack is 4KB; a save path was holding three copies at once. A `static_assert` caps `sizeof(ClockConfig)` so the budget cannot rot the way the old comment did (it claimed ~450). Callers that mutate copy explicitly.
- **`defaults.h` fills a caller's object** (`initDefaultClockConfig(out)`) and exposes individual defaults (`defaultActiveMode()`, `defaultTradingSchedule()`, `defaultClockFormatKey()`, ...) for validation paths that need one fallback. Neither a return-by-value nor a cached static instance was affordable: the first stacked ~700 bytes per call, the second cost the same permanently in DRAM.
- `/config.json` carries `configVersion` (`kConfigSchemaVersion` in `config_serializer.h`). Bump it only for a change older firmware cannot read; adding a patch-semantics field does not need it, and reordering the format catalog is not a schema change because selections are stored as keys.
- `writeAll()` is split into `serializeToTemp()` / `verifyTemp()` / `installVerifiedTemp()` so the serialization `JsonDocument` is destroyed before the verification one is built, instead of both being live.
- `ClockApplication` owns `ConfigManager` and `WifiConnectionManager` and injects them into the web APIs. Configuration saves serialize the complete cached `DeviceConfig` through a verified temporary file and a recoverable backup/rename sequence. Boot prefers a readable JSON-object primary, then `/config.bak`; unreadable files remain intact if neither loads. A surviving backup is retained until the replacement is installed. This is recovery across multiple operations, not one atomic transaction.
- **`storage_manager.h/cpp`** - `StorageManager::ensureMounted(context)` mounts LittleFS on demand with context-rich failure logging; use it instead of calling `LittleFS.begin()` directly.
- **`datetime_validation.h/cpp`** validates real calendar dates (2000-2099) and times. RTC sync and sunset APIs retain their 2020 minimum; saved count-up/countdown dates may use 2000 onward. Config accepts `YYYY-MM-DD HH:MM[:SS]` (space or T), stores canonical seconds, and accepts `kCountupStartNow` only for count-up. Invalid API dates are rejected; invalid disk fields retain defaults. Format indexes are range-checked before narrowing.
  - `formatLocalDateTime()` is `parseLocalDateTime()`'s inverse and the only place a `DateTime` becomes a stored string; `kLocalDateTimeLength` sizes it. The pair must round-trip, because anything that fails to parse back reverts to a default on the next load, so the host suite tests the round trip and both bounds of the supported range. A short buffer truncates rather than overruns, and truncated output deliberately does not parse.
- **`config_validation.h/cpp`** - sanitizers and conversions. Canonical home of `modeName(mode)` / `modeFromName(name, out)` / `sanitizeMode`, `sanitizeFormatIndex`, `sanitizeOptionalFormatIndex` (also accepts `kSameFormat`/-1), `sanitizeBrightness`, `sanitizeBlinkMinutes`, `sanitizeUtcOffsetMinutes`, `sanitizePrintableText`, `sanitizeDisplayMessage`, `sanitizeWifiConfig`. Do not redeclare these elsewhere.

### Networking
- **`wifi_connection_manager.h/cpp`**: application-owned `WifiConnectionManager`, injected into the web portal and `WifiApi`.
  - `begin(config)`: tries STA first (if `staSsid` is set, 15s timeout); falls back to AP. It resolves the effective AP SSID into `apSsid_` **before** the station attempt, because `status()` reports the AP name in station mode too - `resolveApSsid()` is the single place the empty-SSID sentinel turns into `ESP_XXXXXX`, and every consumer reads the resolved member.
  - `startAccessPoint()` checks `softAP()`'s return value and retries once on the compiled defaults: with no station credentials, a rejected credential pair would otherwise leave the device with no way in. The wait for `softAPIP()` is bounded (5s) for the same reason - an open-ended wait hangs the device before the web server starts.
  - `tick()`: handles deferred events (e.g. AP client connected logging).
  - `status()` returns `WifiRuntimeStatus` (mode, connected, ssid, ip, apSsid, apIp); `apSsid` is the **resolved** name, so `/api/wifi/status`, the button's SSID page, and `getNetworkInfo()` all show what is actually on the air.
  - `scanNetworks(doc)`, `connectAndSave(ssid, password)` for web-driven network switching.
- **`web_server.h/cpp`**: application-owned `WebPortal`; `begin()`, `handleClients()` (called every loop), `getNetworkInfo()`, and `scheduleReboot()`.
  - `WebPortal` (internal) owns the `ESP8266WebServer`, `HttpResponder`, and the domain API handlers (`ConfigApi`, `TimeApi`, `LocationApi`, `FileApi`, `WifiApi`). Endpoint domains remain separate where they contain meaningful behavior.
  - **Every page is a static gzipped PROGMEM asset; all dynamic data flows through the JSON APIs.** Page sources live in `web/` (`pages/*.html`, `common.css`, `common.js`); `tools/build_web.py` (a PlatformIO pre-script) gzips them into a `kWebAssets` table that `WebPortal::begin()` registers in one loop. Never build HTML on the server. The route → file mapping lives only in `tools/web_manifest.py`, shared by the build and by `tools/dev_server.py` (edit-reload page development against a live device, no reflash).
  - `web/common.js` owns the shared page helpers (`$`, `api`/`apiPost`, `setStatus`, error/slow-load beacons to `POST /api/client-log`, `reportFieldMismatch`, `setFieldFromConfig`, `toggleSound`/`stopSound`); `web/common.css` is the single stylesheet. Both are served hash-versioned (`?v=`) with an immutable cache header, so each page transfers only its own small body; pages stay `no-cache`.
  - Runs `DNSServer` for captive portal only when in AP mode.
  - UI pages: `GET /`, `/settings`, `/files`, `/format`, `/time`, `/sunset`, `/messages`, `/sound`, `/location`, `/wifi`, `/view`.
  - `GET /api/formats` returns `{countdown|countup|clock: [{key, label}]}`. The key is what the page posts back and what `config.json` stores; sending the index would make a dropdown's value depend on this firmware's table order.
  - REST API: `GET /api/status` (device name, configured mode, and live demo state for the home page), `POST /api/config`, `GET /api/config`, `GET /api/formats`, `GET /api/sounds`, `POST /api/sound/test`, `POST /api/mode`, `POST /api/brightness`, `GET|POST /api/time`, `POST /api/sunset`, `GET /api/zipcode/lookup`, `POST /api/demo/test`, `POST /api/message/test`, `POST /api/field-mismatch`, `GET /api/wifi/status`, `GET /api/wifi/scan`, `POST /api/wifi/connect`.
  - File management: `GET /api/files`, `GET|DELETE /api/file`, `POST /api/file/upload`.
  - **`GET /api/file` refuses `/config.json` with 403.** That file stores the WiFi station password in plain text, which `serializeWifiStatus()` deliberately withholds from `/api/config`; streaming the raw bytes handed it straight back. `/view` reads `/api/config` for that one file. Deliberately a refusal rather than a sanitized body under the same URL - "GET this path returns something other than the bytes at this path" is the leaky abstraction this codebase avoids elsewhere. `FileApi::isCredentialBearingPath()` names the rule. Note that **no endpoint is authenticated**; the threat model is a trusted LAN and that decision is still open.
  - `GET /api/file` mirrors the served bytes to the serial monitor for **`/config.json` only** (`FileApi::logFileContent`); every other path returns immediately. Serial moves at ~7.5 KB/s, so mirroring whatever the browser opens would stall the loop for minutes (`/zipcodes.bin` is 164 KB) - that restriction is the point, and a 4 KB cap backs it up. It runs **after** the response is streamed, rewinds to the served offset, and `yield()`s per 64-byte chunk. `File::read()` returns `int`; never widen it into the `size_t` length for `Serial.write()`.
    - The bytes are re-indented on the way out by `JsonIndenter` (file-local, in `file_api.cpp`), a byte filter that inserts newlines and two-space indents without parsing. **Deliberately not a parse-then-`serializeJsonPretty()`**: this mirror exists to show what is actually on disk, and a config worth reading on the console is often one that no longer parses. A filter cannot fail, allocates nothing, and survives the 4 KB truncation mid-document. It discards existing whitespace rather than adding to it, passes string contents through untouched, and keeps `{}`/`[]` on one line - output verified byte-identical to `serializeJsonPretty()` on the real config.
  - AP-mode radio settings in `wifi_connection_manager.cpp` (11g phy mode, channel survey, 17 dBm TX) are evidence-backed fixes for transfer stalls with power-save phone clients; code comments record what was observed, including two settings that were tried and made things worse. Do not change them without new on-device evidence.
  - **Never judge WiFi/page-load performance while the clock is USB-powered from the PC** (confirmed 2026-07-14): on the PC's USB port next to its 2.4 GHz Bluetooth radio, AP transfers to the phone degrade severely (truncated bodies, ~18 s for a 1.4 KB page) with identical firmware, healthy heap, and fast handlers; on its own power supply away from the PC, loads are fast. This matches the documented USB supply-droop behavior (TX spikes corrupt long frames; that is why TX power is 17 dBm). To separate device work from radio delivery: `time=` in the request log is device-side cost; the browser beacon's `dl=` is delivery time; `TRUNCATED wrote X of Y` means the client stopped ACKing mid-transfer.
- `GET /api/config` mirrors its response body to the serial monitor (`/api/config body:` followed by the JSON sent to the browser). It is streamed with `serializeJsonPretty(doc, Serial)` **after** `sendJsonDocument()` returns - it must stay off the browser's wait and out of RAM. The browser still gets compact bytes; only the console copy is indented, which roughly doubles it (~150ms → ~300ms at 74880 baud). Every settings page hits this endpoint on load, so it is the noisiest line on the console.
- `POST /api/message/test` accepts an optional `"blink": true`, which previews via the blinking `showInfo(msg, 5000)` path instead of the static `showSplash()` - used by Friday and Trading boundary-message previews so they match live behavior.
- `GET /api/sounds` returns `{"songs": [...], "alerts": [...], "available": bool}` (see the Sound section). Sound selection is split across two pages: `/format` carries the **song** each mode's events raise (countdown → at zero, Friday → at sunset, Trading → at open/close) and posts only those names, while `/sound` auditions both kinds and owns enable/volume/startup. Fetches on both pages are chained sequentially - the device serves one connection at a time, so parallel fetches only queue behind each other.
- Each sound row has **one button that toggles between play (▶) and stop (■)**, shared by both pages as `toggleSound(button, selectId)` in `common.js`.
  - The device pushes nothing, so the button learns a sound ended from `durationMs` in the `POST /api/sound/test` response - **not by polling**, which would spend a request per second on a link that serves one connection at a time. `SoundPlayer::durationMs(name)` sums the record's note slots with the same `noteTotalMs()` the player spends per note, so the estimate cannot drift from playback. It is derived, never stored in the catalog, and nothing on the device calls it - only the web preview pays the read.
  - The device plays one sound at a time, so at most one button is ever in the stop state; `common.js` tracks that single button and its revert timer at module scope rather than per row. A missing duration falls back to `SOUND_FALLBACK_MS` (60s) so a button can never stick.
- `/location` edits the persisted device `location`. `/sunset` edits/persists only the `sunset` test fields before posting to `/api/sunset`.
- On `/time`, "Set Time from Browser" updates the RTC/config, resets the shared boundary tracker and Friday sunset cache, and mirrors the new browser-derived values into the Device fields after a successful save.
