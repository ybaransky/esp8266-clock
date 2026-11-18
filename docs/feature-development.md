# Feature Development Policy

## Before coding

1. Read project instructions and relevant architecture, invariants, and platform documents.
2. Inspect the existing implementation, its tests, and configuration.
3. Describe where the feature fits and whether existing mechanisms can be extended.
4. Identify external interfaces, persisted data, concurrency, timing, and error handling affected.
5. For substantial architectural changes, propose a plan and wait for approval.

## Implementation

- Prefer the smallest **coherent** change, not necessarily the fewest lines.
- Reuse existing logic where responsibilities align; do not duplicate state or introduce pass-through wrappers.
- Introduce new classes/interfaces only when they improve ownership, testing, or genuine variation.
- Avoid unrelated refactoring. Small nearby cleanup is acceptable if behavior is preserved and the diff remains reviewable.
- Handle failure, cancellation, startup, and shutdown paths; do not add indefinite waits.
- Update tests and documentation for new behavior and configuration.

## Completion report

Summarize changes, tests actually run, tests not run, remaining risks, and any on-device/on-target verification needed. Do not claim hardware testing from a successful compile.
