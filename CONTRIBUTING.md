# Contributing

Thanks for considering a contribution! This file documents the conventions we follow. The deep technical context lives in [ARCHITECTURE.md](ARCHITECTURE.md) — please read it before making non-trivial changes.

## Before you open a PR

1. **Run the tests** (29 cases, takes < 5 seconds):
   ```bash
   cd test && make clean && make test
   ```
   All must pass.

2. **Build the firmware**:
   ```bash
   cp include/secrets.h.example include/secrets.h    # one-time
   pio run -e d1_mini
   ```
   Must compile without warnings.

3. **If your change affects parsing or state**, add or update a test case in `test/test_main.cpp`. Tests are pinned against real CBPi4 payloads; if a real-world payload changed, please include the new `mosquitto_sub` dump in the PR description.

4. **If your change contradicts a decision documented in [ARCHITECTURE.md](ARCHITECTURE.md)**, that's allowed — but please update the doc to reflect the new rationale. The point of ARCHITECTURE.md is that future-you (or someone else) can understand *why* the code is structured a certain way.

## Code style

- **4-space indent**, no tabs
- **`snake_case`** for functions, variables, and namespaces
- **`PascalCase`** for types (struct, class, enum class)
- **`UPPER_SNAKE_CASE`** for `#define`d constants and macros
- **Namespaces over class hierarchies** — the codebase uses free functions in namespaces rather than singletons or god-classes
- **No `using namespace`** at file scope; explicit qualification is preferred
- **English** for all code, comments, commit messages, and PRs (issue discussions can be in French)
- Comments explain **why**, not **what** — assume the reader can read C++

## Commit message convention

Imperative mood, max 72 chars on the first line, optional body explaining context:

```
Add periodic state dump every 10s for debug visibility

Helps diagnose intermittent state inconsistencies between MQTT
callbacks and display rendering. Output prefixed [state] for grep.
```

If your commit closes an issue, add `Closes #N` in the body.

## Module boundaries

The four-way separation between parsing, state, network, and display is what makes the Phase 1 → Phase 2 port realistic. Try not to violate it:

- `cbpi_proto` — pure functions, no I/O, no globals other than `state::g`. Host-testable.
- `state` — POD struct, no logic.
- `net_*` — talk to the network, mutate state via `cbpi_proto`. No display calls.
- `display`, `heartbeat` — read state, drive the hardware. No network calls.

If your feature crosses a boundary, that's usually a sign that one module needs an interface change, not that the boundary should be ignored.

## When in doubt

Open a draft PR with a question, or open an issue first to discuss. Better to clarify before writing code than after.
