# Architecture Review Procedure

A review is **read-only** unless the developer separately authorizes implementation.

1. Inspect project instructions, build configuration, source tree, and project-specific invariants.
2. Draw a concise dependency/ownership map and describe the main runtime data flow.
3. Identify complexity hotspots: duplicate state, duplicated logic, circular dependencies, unnecessary abstractions, large modules, blocking work, fragile error handling, or excessive coupling.
4. Distinguish architectural problems from intentional constraints and tradeoffs.
5. Recommend no more than five high-value changes initially. For each: exact files/functions, why, proposed simplification, risks, expected benefit, and verification.
6. Classify each change as routine, structural, or critical under `refactoring.md`.
7. List unknowns and any hardware/runtime evidence needed. Do not invent metrics.

Suggested output: baseline summary; preserved invariants; prioritized recommendations; test/verification plan; explicitly deferred ideas.
