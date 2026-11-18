# ESP8266 Clock — Build and Test

Firmware builds are performed on Windows; native Unity tests run under WSL with an isolated build directory. **Do not run `pio run` under WSL.** Do not upload firmware or LittleFS without explicit approval.

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

# Clean build artifacts
pio run --target clean

# Host unit tests for the pure modules (schedule, display formats, datetime, beep envelopes, timezone rules).
# Must run where a host C++ compiler exists. On the author's Windows box that
# means WSL: Smart App Control is enforced and blocks unsigned executables, so
# MinGW's cc1.exe/cc1plus.exe cannot launch. PLATFORMIO_BUILD_DIR is required -
# see "Sharing .pio between two PlatformIO installs" below.
PLATFORMIO_BUILD_DIR=$HOME/.cache/pio-build/esp8266-clock pio test -e native
# Or in VS Code: Ctrl+Shift+P -> Tasks: Run Test Task (.vscode/tasks.json)

# Format-catalog invariants: key uniqueness, defaults resolvable, overflow
# fallbacks present. Runs automatically as a pre-script for `d1_mini` firmware builds.
python tools/check_formats.py

# data/config.json equals the compiled defaults, field for field. Also runs
# automatically as a pre-script for `d1_mini` firmware builds.
python tools/check_config_defaults.py
```

Validation is primarily by flashing the firmware and observing behavior on device. Serial output at 74880 baud includes stack traces decoded by `monitor_filters = esp8266_exception_decoder`.

Automated layers back that up:
- **`tools/check_formats.py`** runs as a pre-script on every `d1_mini` firmware build and fails it on a duplicate format key, a default naming a nonexistent format, or a combined `hhh:mm` format with no split fallback. It parses the C++ rather than compiling it, so it needs only Python.
- **`tools/check_config_defaults.py`** runs as a pre-script on every `d1_mini` firmware build and fails it when `data/config.json` disagrees with the compiled defaults (value or JSON type), is missing a known field, or carries one it does not know. It also parses the C++, resolving each default the way the firmware does: a `fillDefaults()` assignment in `defaults.cpp` wins, else the member initializer in `config.h` / `beep_pattern.h`; Trading sessions come from `defaultTradingSchedule()` and WiFi from `defaultWifiConfig()`. `time.timezone`, `location`, and `sunset` are device-specific and not compared. Its `expected_values()` function returns the mapping from each JSON path to its default, so a new config field goes there as well as into `data/config.json`.
- **`test/` holds Unity suites** for `schedule.cpp`, `display_format.cpp`, `datetime_validation.cpp`, `beep_pattern.cpp`, and `timezone_rule.cpp` - the modules that are pure by design. `test_timezone` also parses every rule in `assets/timezones.csv`, so it must run from the project root (PlatformIO does). They run via `pio test -e native` wherever a host compiler exists; `test/stubs/` supplies host stand-ins for `Arduino.h` and RTClib's `DateTime`.

These validation pre-scripts are not configured for `native`; run them separately when checking only native tests.

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
defect, since `long` is 64 bits on the host and 32 on the ESP8266. If a
toolchain upgrade one day fails the tests on a new warning, read it and fix it
or drop the flag; do not reach for `-Wno-<whatever>` without understanding what
it hides.

The firmware build is warning-clean with `-Wall -Wextra` but **not** `-Werror`,
because its `build_flags` also reach the Arduino core and five third-party
libraries. Gating only our own sources there would need `build_src_flags`. `-Wno-deprecated-copy` is applied to C++ only (via `tools/cxx_flags.py`) because RTClib declares `DateTime`'s copy constructor without its assignment operator, and eleven instances of that third-party defect would drown our own warnings.

## Required verification

- Check `git status` before modifying files.
- Inspect `platformio.ini` before selecting an environment.
- Run `pio run` on Windows for a firmware build.
- Run native tests in WSL with `PLATFORMIO_BUILD_DIR=$HOME/.cache/pio-build/esp8266-clock pio test -e native` when available.
- For pure-module changes, extend the corresponding Unity tests.
- On-device testing remains required for hardware, timing, display, radio, and peripheral behavior; never claim it happened unless performed.
- Do not change the Windows/WSL build directory arrangement.
