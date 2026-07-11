# Redesign project UI

> suggest a better and cleaner design for this project

<details><summary>4 previous messages</summary>

> I’ll review the current module boundaries, build configuration, and main control flow, then propose a cleaner target architecture with an incremental migration path that preserves the hardware-sensitive invariants.
>
> <details><summary>Ran 2 commands</summary>
>
> - Ran `rg --files -g '!\.pio/**' -g '!\.git/**'`
> - Ran `Get-Content platformio.ini; Get-Content src\main.cpp; Get-Content src\display_manager.h; Get-Content src\config.h; Get-Content src\web_server.cpp -TotalCount 180`
>
> </details>
>
> The project is already sensibly decomposed by feature; the main cleanup opportunity is not “more files,” but clearer ownership and dependency direction. I’m checking where globals and cross-module calls currently create hidden coupling so the recommendation stays practical for an ESP8266.
>
> <details><summary>Ran 3 commands</summary>
>
> - Ran `rg "^extern |^[A-Za-z_][A-Za-z0-9_:<> ]+ [A-Za-z_][A-Za-z0-9_]*;$" src -g "*.h"; rg "#include \"" src -g "*.cpp"; Get-ChildItem src -Filter *.cpp | Sort-Object Length -Descending | Select-Object Name,Length`
> - Ran `rg '^extern ' src -g '*.h'; rg '#include "' src -g '*.cpp'; Get-ChildItem src -Filter *.cpp | Sort-Object Length -Descending | Select-Object Name,Length`
> - Ran `Get-Content src\config_api.h; Get-Content src\friday_mode.h; Get-Content src\clock_state.h; Get-Content src\page_manager.h`
>
> </details>
</details>

The project already has good feature separation. The cleaner design I’d recommend is a small application core with explicit ownership, while retaining the cooperative Arduino loop and avoiding heavyweight abstractions.

## Main issue

The current architecture is modular at the file level but coupled through global singletons and free functions:

- `displayManager`
- `configManager`
- `wifiConnectionManager`
- `pageManager`
- `segmentDisplay`
- RTC and Friday-mode free functions
- APIs that directly reach into several of these services

This makes dependencies difficult to see, configuration changes non-transactional, and host-side testing unnecessarily hard.

## Recommended target design

```text
main.cpp
    └── ClockApplication
          ├── ClockController
          │     ├── ClockState
          │     ├── FridaySchedule
          │     └── DisplayController
          ├── ConfigService
          ├── NetworkService
          ├── WebPortal
          ├── RtcService
          └── ButtonController

Infrastructure
    ├── Ds3231Rtc
    ├── Tm1637Display
    ├── LittleFsConfigStore
    └── Esp8266Network
```

### 1. Add one composition root

Create a `ClockApplication` that owns all long-lived components and defines startup and loop order:

```cpp
class ClockApplication {
 public:
  void begin();
  void tick(uint32_t nowMs);

 private:
  Ds3231Rtc rtc_;
  Tm1637Display display_;
  LittleFsConfigStore configStore_;
  DisplayController displayController_;
  ClockController clockController_;
  NetworkService network_;
  WebPortal web_;
  ButtonController buttons_;
};
```

Then `main.cpp` becomes approximately:

```cpp
ClockApplication app;

void setup() {
  app.begin();
}

void loop() {
  app.tick(millis());
}
```

This centralizes lifecycle and eliminates the hidden initialization order created by globals.

### 2. Make `ClockController` the application coordinator

Currently, coordination is spread across `main.cpp`, `clockApplySettings()`, `fridayModeTick()`, API handlers, and `DisplayManager`.

Instead, give one controller responsibility for application actions:

```cpp
class ClockController {
 public:
  void applyConfig(const ClockConfig& config);
  void onSecondBoundary(const DateTime& now);
  void setTime(const DateTime& now);
  void setMode(Mode mode);
  void showMessage(const char* message);
};
```

The web UI and button controller should issue commands to this controller. They should not manipulate display, RTC, Friday mode, and persistence independently.

For example:

```text
POST /api/config
    → parse and validate request
    → ClockController::applyConfig()
        → save configuration
        → update Friday schedule
        → update base display view
```

This provides one authoritative path for every configuration change.

### 3. Separate display state from display rendering

`DisplayManager` currently owns several responsibilities:

- persistent settings
- base-view selection
- overlay lifecycle
- countdown/countup calculations
- display formatting
- rendering cadence
- physical output

Split it into three focused parts:

```text
DisplayController
    Owns base view and overlay state
    Resolves what should appear

DisplayRenderer
    Converts resolved state into three four-character rows
    Pure logic, independently testable

Tm1637Display
    Writes rows and brightness to hardware
```

Keep `DisplayScheduler` as a small timing policy owned by `DisplayController`.

A useful render boundary is:

```cpp
struct DisplayFrame {
  char rows[3][5];
  bool colon;
};

DisplayFrame renderDisplay(
    const ResolvedDisplayState& state,
    const DateTime& now,
    uint16_t msIntoSecond);
```

The hardware layer should only receive a completed `DisplayFrame`.

### 4. Model overlays explicitly

Preserve the important base-view/overlay rule, but make the states harder to misuse.

Since memory matters on ESP8266, a tagged structure is preferable to `std::variant`:

```cpp
struct OverlayState {
  OverlayKind kind = OverlayKind::kNone;
  OverlayTiming timing;

  MessageOverlay message;
  DemoOverlay demo;
  PagedOverlay pages;
};
```

Only `DisplayController` should interpret the tag and access its matching payload. Other modules should use methods such as `showMessage()` and `clearOverlay()`.

Also add `kNone` rather than maintaining both `hasOverlay_` and an overlay value. That removes a state combination that can become inconsistent.

### 5. Turn Friday mode into a pure policy

Friday mode is business logic, not display logic. Make it calculate a result without touching global state:

```cpp
struct FridayResult {
  ViewState view;
  FridayPhase phase;
  bool crossedFridaySunset;
};

FridayResult evaluateFridayMode(
    const DateTime& now,
    const FridaySettings& settings,
    FridayRuntimeState& state);
```

`ClockController` then decides whether to update the base view or show the sunset overlay.

This makes boundary cases—Thursday midnight, Friday sunset, boot after sunset, timezone changes—testable without hardware.

### 6. Split the large configuration object

`ClockConfig` currently combines several domains. Use nested structures while preserving the existing serialized JSON schema:

```cpp
struct ClockConfig {
  DisplayConfig display;
  CountdownConfig countdown;
  CountupConfig countup;
  FridayConfig friday;
  LocationConfig location;
  TimeConfig time;
  MessageConfig messages;
};
```

Example:

```cpp
struct DisplayConfig {
  uint8_t brightness;
  bool use12Hour;
  FormatSelection formats;
};
```

The serializer can continue emitting existing fields, so this refactoring need not break the web UI or saved configuration.

Also use a single operation for loading:

```cpp
struct DeviceConfig {
  ClockConfig clock;
  WifiConfig wifi;
};

LoadResult ConfigService::load();
```

That avoids repeatedly opening and parsing the same configuration file during startup.

### 7. Keep HTTP handlers thin

`ConfigApi` is one of the largest behavioral modules. It should mainly:

1. Parse the HTTP request.
2. Convert it into a command or DTO.
3. Call the controller.
4. Serialize the result.

It should not contain clock behavior.

Consider API grouping:

```text
ClockApi       mode, brightness, messages, formats
TimeApi        time read/sync
LocationApi    location, zipcode, sunset preview
ConfigApi      complete config import/export
WifiApi        network operations
FileApi        filesystem operations
```

Route registration can remain centralized in `WebPortal`.

### 8. Move HTML out of the 58 KB translation unit

[html.cpp](./src/html.cpp) is much larger than every other source file.

Two reasonable options:

- Keep critical captive-portal pages in `PROGMEM`, but split each page into its own source file.
- Store the full UI in LittleFS and retain only a small recovery/setup page in firmware.

I prefer the second design if `uploadfs` is reliably part of deployment. The first is safer if the firmware must remain usable after LittleFS corruption.

### 9. Organize by responsibility

A gradual directory layout could be:

```text
src/
  app/
    clock_application.*
    clock_controller.*
    clock_state.*

  domain/
    display_state.*
    friday_schedule.*
    clock_format.*
    config_types.*

  drivers/
    ds3231_rtc.*
    tm1637_display.*
    button_input.*

  services/
    config_service.*
    network_service.*
    sunset_service.*

  web/
    web_portal.*
    clock_api.*
    config_api.*
    wifi_api.*
    file_api.*

  platform/
    hardware.*
    logging.*
    storage.*
```

PlatformIO compiles sources recursively, so this does not require separate libraries.

## What I would not change

I would retain:

- The non-blocking cooperative `loop()`.
- SQW-driven second processing.
- RTC-cached rendering.
- Phase-locked tenths.
- Base view plus temporary overlay semantics.
- Fixed-size display buffers.
- The centralized JSON serializer.
- Explicit ESP8266 hardware drivers.

An event bus, dependency-injection framework, RTOS tasks, or generic repository framework would make this firmware harder to understand without providing meaningful benefit.

## Suggested migration order

1. Introduce `ClockApplication` without changing behavior.
2. Convert globals into objects owned by it.
3. Introduce `ClockController` and route config/API actions through it.
4. Extract Friday mode into a pure policy.
5. Separate `DisplayController`, pure renderer, and TM1637 driver.
6. Restructure `ClockConfig` while preserving JSON compatibility.
7. Split HTTP APIs and HTML assets.
8. Add native tests for format rendering, Friday transitions, validation, and overlay expiration.

This order keeps every step buildable and minimizes the risk of disturbing the timing-sensitive RTC and display behavior.

> this is a good design except i still want to keep the HTML in the html.cpp file for now.
> but before i let you do this, i want to create a new branch of my git project (the name of the new branch is codex) and implement the new deign in the codex brnach.  if i like the new code, i want to then make this codex branch the main branch, and if i don't i will discard it.  explain to me in detail how to do this with git locally and how to push this remotely

<details><summary>2 previous messages</summary>

> I’ll check your current branch, working-tree state, and configured remotes so the commands match this repository. I won’t create or switch branches yet.
>
> <details><summary>Ran a command</summary>
>
> - Ran `git status --short --branch; git remote -v; git branch --all`
>
> </details>
</details>

Your repository is currently in an ideal starting state:

- Current branch: `main`
- Working tree: clean
- Remote: `origin`
- Remote URL: `https://github.com/ybaransky/esp8266-clock.git`
- Local `main` tracks `origin/main`

I’ll also keep the HTML in `src/html.cpp` during the redesign.

## 1. Create the local `codex` branch

First, make sure your local `main` is current:

```powershell
git switch main
git pull --ff-only origin main
```

Then create and switch to the new branch:

```powershell
git switch -c codex
```

This creates `codex` from the current commit on `main`.

Verify:

```powershell
git status
git branch
```

You should see:

```text
* codex
  main
```

At this point, changes and commits will belong to `codex`. The `main` branch remains unchanged.

## 2. Push the new branch to GitHub

Publish `codex` and establish its upstream tracking branch:

```powershell
git push -u origin codex
```

The `-u` option connects local `codex` to `origin/codex`. Afterward, while on this branch, ordinary commands are enough:

```powershell
git push
git pull
```

You can verify all branches with:

```powershell
git branch --all
```

Expected result:

```text
* codex
  main
  remotes/origin/codex
  remotes/origin/main
```

## 3. Implement and commit the redesign

Before making changes, confirm you are on the correct branch:

```powershell
git branch --show-current
```

It should print:

```text
codex
```

After each logical portion of the redesign:

```powershell
git status
git diff
git add src platformio.ini
git commit -m "Introduce application composition root"
git push
```

I recommend several focused commits rather than one large commit. For example:

```text
Introduce ClockApplication composition root
Route clock actions through ClockController
Extract Friday scheduling policy
Separate display state from rendering
Restructure configuration types
Reorganize web API controllers
```

This makes the redesign easier to inspect, test, or partially revert.

Because `git add src platformio.ini` is intentionally scoped, it is safer than blindly staging every new unrelated file. If new design documentation is added, stage it explicitly:

```powershell
git add docs
```

## 4. Move between the old and redesigned versions

To view the original code:

```powershell
git switch main
```

To return to the redesign:

```powershell
git switch codex
```

Git may refuse to switch if you have uncommitted changes that conflict with the destination branch. The safest response is usually to commit them on `codex`:

```powershell
git add src platformio.ini
git commit -m "Save redesign work in progress"
git switch main
```

For truly temporary work, you can use a stash:

```powershell
git stash push -m "Temporary redesign work"
git switch main
```

Return and restore it with:

```powershell
git switch codex
git stash pop
```

Committing to the experimental branch is generally clearer than keeping a long-lived stash.

## 5. Review the redesign before accepting it

Build the `codex` branch:

```powershell
git switch codex
platformio run
```

Compare the branch against `main`:

```powershell
git diff main...codex
git log --oneline --graph --decorate main..codex
```

To see a compact file-level summary:

```powershell
git diff --stat main...codex
```

You can also review it on GitHub by opening a pull request from `codex` into `main`. A pull request is optional for a personal repository, but GitHub’s file-by-file review interface is useful even if you are the only reviewer.

Do not merge the pull request until you are satisfied with the implementation.

## 6. If you like the redesign

Normally, you do not rename `codex` to `main`. You merge the work from `codex` into the existing `main`. This preserves the established branch name and GitHub configuration.

First, make sure all `codex` work is committed and pushed:

```powershell
git switch codex
git status
platformio run
git push
```

Then update and merge into `main`:

```powershell
git switch main
git pull --ff-only origin main
git merge --ff-only codex
git push origin main
```

Because `codex` was created directly from `main`, `--ff-only` should work as long as no independent commits were added to `main`. It advances `main` to the same commit as `codex` without creating an unnecessary merge commit.

Verify:

```powershell
git log --oneline --graph --decorate --all
git status
```

At that point:

- Local `main` contains the redesign.
- `origin/main` on GitHub contains the redesign.
- The GitHub default branch remains `main`.
- `codex` can be retained temporarily or deleted.

### If `--ff-only` fails

That means `main` and `codex` both acquired independent commits. Merge normally:

```powershell
git merge codex
```

If conflicts occur, Git will mark the affected files. Resolve them, then run:

```powershell
git add <resolved-files>
git commit
git push origin main
```

Alternatively, stop the merge without changing either branch:

```powershell
git merge --abort
```

## 7. Delete `codex` after accepting it

Once `main` is pushed and verified, delete the local experimental branch:

```powershell
git branch -d codex
```

Delete the remote GitHub branch:

```powershell
git push origin --delete codex
```

The lowercase `-d` checks that the branch has been merged before deleting it.

## 8. If you do not like the redesign

First switch away from `codex`:

```powershell
git switch main
```

Confirm that `main` still contains the original version:

```powershell
git status
git log --oneline --decorate -5
platformio run
```

Then delete the local experimental branch:

```powershell
git branch -D codex
```

Use uppercase `-D` because Git knows the branch has not been merged and would reject the safer `-d`.

Delete the remote experimental branch:

```powershell
git push origin --delete codex
```

Your local and remote `main` branches remain unchanged.

## Recommended complete workflow

```powershell
# Create the experiment
git switch main
git pull --ff-only origin main
git switch -c codex
git push -u origin codex

# Implement and test
platformio run
git add src platformio.ini
git commit -m "Implement cleaner clock architecture"
git push

# Accept it
git switch main
git pull --ff-only origin main
git merge --ff-only codex
platformio run
git push origin main
git branch -d codex
git push origin --delete codex
```

Or discard it:

```powershell
git switch main
git branch -D codex
git push origin --delete codex
```

The key safety check before every redesign commit is:

```powershell
git branch --show-current
```

It must say `codex` until you deliberately decide to merge the redesign into `main`.
