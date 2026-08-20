---
name: Testability Split at the Platform Boundary
globs: ["lib/**", "src/**", "test/**"]
paths: ["lib/", "src/", "test/"]
topics: ["testing", "architecture", "modularity", "tdd", "embedded"]
priority: low
auto_generated: true
derived_from: [sensor-monitoring-dashboard]
evidence_count: 1
last_validated: 2026-08-20
---

# Testability Split at the Platform Boundary

When a module must run under both a hosted test environment and a device/production
environment, split it at the point where a platform-specific primitive is introduced. The
pure half carries the logic and the tests; the thin wrapper carries the primitive and holds
no logic of its own.

- The split point is wherever a **platform primitive** enters: a FreeRTOS mutex, a DOM
  handle, a socket, a filesystem call, a clock. Everything upstream of it is pure.
- The wrapper must be genuinely thin — acquire, delegate, release. If the wrapper contains
  arithmetic or branching, the split is in the wrong place and that logic is untestable.
- Apply this before writing code, not after. Retrofitting the split means rewriting the
  module and its tests; a design review is the cheap place to catch it.
- This generalizes far beyond its usual framing. One task applied the identical split to a
  ring buffer (mutex), a debounce state machine (GPIO reads), backoff math (timers), a JSON
  serializer (output handle), and browser dashboard logic (DOM/fetch) — the last in a
  different language from the first four.
- The payoff is not only unit coverage: it makes an entire hardware-dependent project
  testable in CI with **no device attached**, which is often the difference between having
  a test suite and not having one.
- A pure module's dormant code paths still get exercised eventually. Write tests for the
  error and timeout branches even when no caller reaches them yet.
