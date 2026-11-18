# ESP8266 Clock — Shared Instructions for Codex and Claude Code

This is the common AI instruction entry point. `CLAUDE.md` imports this file. **Read the relevant linked docs explicitly; links are not automatically loaded.**

## Before any change
- Inspect `git status`, `platformio.ini`, relevant implementation and tests.
- Read `docs/project.md` and `docs/invariants.md`, plus the documents relevant to the task.
- Treat `src/hardware.h` as authoritative for GPIO assignments; never change pin mappings without approval.
- If code and documentation disagree, report the mismatch; do not silently change either.
- Preserve existing uncommitted work. Do not commit, push, create branches, flash firmware, upload LittleFS, or deploy without explicit permission.

## Feature development and refactoring
- Follow `docs/feature-development.md`, `docs/refactoring.md`, and `docs/architecture-review.md`.
- Inspect existing functionality before adding helpers, state, modules, or interfaces; prefer extending a suitable existing mechanism.
- Small, behavior-preserving local cleanup is acceptable within an approved feature task; request approval for structural or critical changes.
- When asked for an architecture review, analyze only: **do not edit files**.
- Respect deliberate concrete constructor injection, pure schedule/render logic, and the limited `RebootScheduler` interface; do not introduce abstractions just to satisfy a generic SOLID rule.
- Preserve `Mode` / `View` / `Overlay` separation, RTC UTC and SQW timing contracts, configuration persistence rules, and the current network threat model.

## Required reading by task
- Overview and ownership: `docs/project.md`, `docs/architecture.md`
- Nonnegotiable correctness: `docs/invariants.md`
- Pin map and boot behavior: `docs/hardware.md`, `docs/platforms/esp8266.md`, `docs/buzzer_wiring.md`
- RTC and logging contracts: `docs/logging-and-rtc.md`
- Local build/test commands: `docs/build-and-test.md`, `docs/platformio.md`
- Coding conventions: `docs/coding-standards.md`, `docs/arduino-framework.md`
- Feature work and refactoring: `docs/feature-development.md`, `docs/refactoring.md`, `docs/architecture-review.md`
- Git: `docs/git-workflow.md`; reusable task prompts: `prompts/`
- Sound: `docs/sounds.md`

## Verification
- Build firmware on Windows with `pio run` when available.
- Native tests run under WSL with **separate** `PLATFORMIO_BUILD_DIR`; see `docs/build-and-test.md`.
- Run relevant Python validation and Unity suites; report what actually ran.
- Device-level verification is needed for timing, GPIO, display, and Wi-Fi changes; never claim hardware testing unless performed.
- Summarize affected files, tests, risks, and any documentation updates.

## Documentation provenance
- Project-specific documents were migrated from two uploaded instruction files, preserved verbatim in `docs/reference/`.
- On 2026-11-17, `docs/architecture.md` and `docs/pcb-specification.md` were checked sentence by sentence against the source, and the main claims in `hardware.md`, `invariants.md`, `logging-and-rtc.md`, and `build-and-test.md` were spot-checked. Code still wins: report any mismatch you find.
