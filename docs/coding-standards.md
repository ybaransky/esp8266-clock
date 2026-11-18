# Embedded C++ Coding Standards

- Follow the project's existing naming, formatting, and language-standard settings.
- Prefer simple composition, explicit ownership, and concrete dependencies when there is only one implementation; interfaces/virtual dispatch require a concrete benefit.
- Use `constexpr`, `enum class`, `const`, references, and fixed-size storage where they improve safety and clarity.
- Avoid unnecessary dynamic allocation in hot paths, unbounded recursion, and large stack temporaries on constrained targets.
- Avoid `String` churn in tight loops on small MCUs; prefer bounded buffers where appropriate.
- Keep hardware-independent algorithms separable and host-testable where reasonable.
- Keep ISRs short and follow target-specific synchronization/atomicity requirements; `volatile` is not a general concurrency primitive.
- Favor readable functions and coherent modules; do not mechanically split functions or add wrappers merely to satisfy style rules.
- Document non-obvious rationale, hardware constraints, and intentional deviations from standard conventions.
- Review warnings and binary/resource impact; avoid hiding warnings without understanding them.

---

## ESP8266 Clock Conventions (project-specific overrides)

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
  `camelCase` functions and locals, and `kPascalCase` constants and enum
  values. Reserve `ALL_CAPS` for macros and established hardware pin
  identifiers.
- **Class member variables use an `m_` prefix followed by camelCase**
  (`m_status`, `m_renderInvalidated`) - a deliberate departure from Google's
  trailing underscore. Public fields of plain data structs (`ClockConfig`,
  `ViewState`, ...) take no prefix; they are accessed as `config.display.brightness`
  and are the struct's interface, not its hidden state. Do **not** use a leading
  underscore (`_status`): `_` followed by an uppercase letter is reserved
  everywhere, any leading underscore is reserved at global scope, and the
  ESP8266 core already defines `_min`/`_max` as function-like macros.
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
