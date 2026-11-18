# ESP8266 Clock Documentation

This directory contains both **reusable development policies** and **project-specific technical documentation**. The project-specific docs were migrated from the original `CLAUDE.md` and `AGENTS.md`. On 2026-11-17, `architecture.md` and `pcb-specification.md` were checked sentence by sentence against the source, and the main claims in `hardware.md`, `invariants.md`, `logging-and-rtc.md`, and `build-and-test.md` were spot-checked. If code and docs disagree, the code is authoritative; report the mismatch.

## Reusable, copyable policies
- `feature-development.md`, `refactoring.md`, `architecture-review.md`, `git-workflow.md`
- `arduino-framework.md`, `platformio.md`
- `coding-standards.md` begins with reusable C++ conventions, then adds clock-specific overrides; do not copy its project-specific section to unrelated projects.
- `platforms/esp8266.md` (selected MCU platform)

## Clock-specific documents
- `project.md` — overview and source-of-truth locations
- `architecture.md` — detailed modules, display/schedule/config/network contracts
- `hardware.md` — pin map and boot constraints
- `pcb-specification.md` — PCB specification, BOM, nets, power, layout, open fabrication decisions (formerly root `HARDWARE.md`)
- `buzzer_wiring.md` — detailed buzzer transistor wiring and validation (formerly root `WIRING.md`)
- `sounds.md` — beep patterns, timing, and priority rules (formerly root `SOUNDS.md`)
- `invariants.md` — critical correctness rules
- `logging-and-rtc.md` — RTC service and logging details
- `build-and-test.md` — Windows firmware builds, WSL native tests, scripts
- `reference/original-CLAUDE.md` and `reference/original-AGENTS.md` — complete, unchanged source documents for audit and recovery

The archived originals retain their original link paths, which may no longer resolve from `reference/`. Use the current documentation listed above; the originals remain unchanged for audit and recovery.

The former root documents have moved: `HARDWARE.md` to `pcb-specification.md`, `WIRING.md` to `buzzer_wiring.md`, and `SOUNDS.md` to `sounds.md`. Their root paths have been removed in the current working tree. `src/hardware.h` remains the authority on pin assignments.
