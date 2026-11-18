# Refactoring Policy

## Objective

As features accumulate, reduce **accidental complexity** without losing clarity, correctness, or testability. Do not equate fewer functions, classes, or lines with better design. Prefer understandable ownership, simple data flow, and fewer independent sources of truth.

## Three levels

1. **Routine cleanup**: local naming, duplication removal, and straightforward simplification within the requested scope may accompany a feature if behavior and interfaces are unchanged.
2. **Structural refactoring**: moving responsibilities, combining modules, changing ownership or interfaces requires a written proposal and approval before edits.
3. **Critical behavior**: changes to hardware pins, persistent formats, timing/clock semantics, security, safety, deployment, or other project invariants require explicit approval and target-specific validation.

## Read-only review first when requested

- Map modules, ownership, dependencies, entry points, state transitions, and error paths.
- Find duplicated calculations, redundant state, wrapper-only functions, over-generalized abstractions, and unnecessarily tangled responsibilities.
- Also identify code that is **intentionally** separate (testability, hardware boundaries, fault containment).
- Rank recommendations by impact, risk, and effort; cite exact files/functions and explain verification.
- Do not edit files during a review-only request.

## Implementation after approval

- Establish baseline build/tests and, where available, resource metrics.
- Change one coherent concern at a time; preserve external behavior.
- Add characterization tests where existing behavior is poorly covered.
- Run targeted and broader tests; inspect `git diff` for unintended changes.
- Report measured resource deltas, not guesses. Keep unverified hardware behavior explicitly marked.

## Useful signals, not targets

Track module/function count, unusually large functions, coupling, duplicated logic, mutable global state, warnings, test coverage, flash/RAM or CPU/I/O as relevant. No metric should be optimized in isolation.
