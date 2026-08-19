# System Architecture Patterns

This file documents the architectural patterns, design patterns, and system structure used in this project. It helps developers understand the system's organization and maintain consistency when extending functionality.

> **Status (2026-08-19)**: greenfield. No architecture exists yet — `src/main.c` is an empty
> `app_main()`. The Guiding Principles below are the *starting* conventions for an ESP-IDF
> firmware project; confirm or replace them during the first `/bmb:creative` or `/bmb:plan` pass,
> and fill the empty sections as real structure appears.

## Guiding Principles

<!--
  These principles govern ALL architectural and implementation decisions.
  Agents MUST validate their work against these principles.
  Deviations require explicit justification in a creative/architecture decision document
  with a trade-off analysis referencing the specific principle being overridden.
-->

| Principle | Description |
|-----------|-------------|
| Hardware Abstraction | Sensor and actuator access goes through a driver interface in `lib/`; application logic never calls IDF peripheral APIs (`i2c_*`, `adc_*`, `gpio_*`) directly. This is what makes logic testable without hardware. |
| Fail-Safe Defaults | A sensor read failure, network loss, or bad reading must never leave an actuator energized or the device wedged. Every failure path has a defined safe state. |
| No Blocking in `app_main` | Long-running work lives in FreeRTOS tasks with explicit stack sizes and priorities; `app_main` wires things up and returns or blocks on a supervisory primitive. |
| Explicit Error Handling | Every IDF call returning `esp_err_t` is checked. Use `ESP_ERROR_CHECK` only where an abort is genuinely the correct response; otherwise handle and log. |
| Configuration Is Not Hard-Coded | Pin assignments, thresholds, intervals, and credentials come from Kconfig (`menuconfig`) or NVS — not string/number literals scattered through source. Secrets are never committed. |
| Structured Logging | Use the IDF logging macros (`ESP_LOGI`/`ESP_LOGW`/`ESP_LOGE`) with a per-module `static const char *TAG`. No `printf` for diagnostics. |

## System Architecture

### High-Level Architecture
```
[To be defined — no components exist yet.]

Expected shape for a hydroponic monitor (confirm during planning):

┌──────────────┐    ┌───────────────┐    ┌──────────────┐    ┌─────────────┐
│   Sensors    │───▶│   Drivers     │───▶│  Monitoring  │───▶│  Transport  │
│ (I2C/ADC/1W) │    │   (lib/)      │    │     Task     │    │ (Wi-Fi/MQTT)│
└──────────────┘    └───────────────┘    └──────┬───────┘    └─────────────┘
                                                │
                                                ▼
                                         ┌──────────────┐
                                         │  Actuators   │
                                         │  (optional)  │
                                         └──────────────┘
```

### Component Responsibilities
- **[Component Name]**: [Primary responsibility, dependencies]

### Data Flow Patterns

#### [Pattern Name]
```
[Diagram showing data flow]
```
- **Trigger**: [What initiates this flow]
- **Steps**: [Key processing steps]
- **Output**: [Result or side effects]

## Design Patterns Used

[None recorded yet. Document each pattern as it is introduced, with a `path/to/file.c:line`
anchor.]

## Integration Patterns

### [Integration Name]
- **Type**: [To be defined]
- **Protocol**: [To be defined]
- **Direction**: [To be defined]
- **Contract**: [Schema location or documentation]

## Code Organization Patterns

<!--
  This section records SOURCE-file conventions so an agent never has to *infer*
  a new file's language or extension at build time. It is the authoritative
  anchor: when a new file is created, its extension is resolved from the table
  below, NOT from whichever file happens to sit next to it.
-->

### Source Language & Default Extensions
- **Primary language**: C (ESP-IDF) — `.c` for implementation, `.h` for headers
- **`.cpp`/`.hpp` permitted?**: Not currently. The scaffold is C-only. Introducing C++ requires
  an explicit decision (it changes `idf_component_register` behaviour and linkage; C headers
  consumed from C++ need `extern "C"` guards). Record that decision here if it is made.
- **Type-checking enforced**: The compiler is the gate — `pio run` must build clean. Treat
  warnings as defects; consider adding `build_flags = -Wall -Wextra` to `platformio.ini`.

### File Extension by Directory / Role

| Directory / Role | Extension | Notes |
|------------------|-----------|-------|
| `src/` | `.c` | Application sources; PlatformIO globs `src/*.*` into one IDF component |
| `include/` | `.h` | Headers shared across `src/` translation units |
| `lib/<name>/src/` | `.c` | Private library implementation |
| `lib/<name>/include/` | `.h` | Private library public interface |
| `test/test_<suite>/` | `.c` | Unity test suites |

### Module Structure
- **New driver / subsystem shape**: `lib/<name>/{include/<name>.h, src/<name>.c}` — one library
  per hardware device or cohesive subsystem, so it can be unit-tested and reused independently.
- **Application glue**: `src/` holds `app_main()` plus task entrypoints that compose the `lib/`
  modules. Keep `src/main.c` thin.
- **Naming**: `snake_case` for functions and files; prefix a module's public symbols with the
  module name (e.g. `ph_sensor_read()`), since C has no namespaces.
- **Headers**: use `#pragma once` or include guards consistently — pick one on the first header
  created and record the choice here.

## Testing Patterns

### Test Organization
- **Test location**: separate directory — `test/`, one subdirectory per suite
  (PlatformIO Test Runner requirement)
- **File mapping**: one test suite per `lib/` module
- **Naming convention**: `test/test_<module>/test_<module>.c`

### Test Grouping
- **Within-file structure**: group by behavior/scenario; each `TEST_CASE`/`RUN_TEST` covers one
  observable behavior
- **Setup sharing**: Unity `setUp()` / `tearDown()` per suite

### Test Framework & Style
- **Framework**: Unity (bundled with PlatformIO / ESP-IDF)
- **Assertion style**: `TEST_ASSERT_*` macros
- **Mocking approach**: [To be decided.] The Hardware Abstraction principle above is what makes
  this possible — inject a driver function-pointer struct (or link a stub implementation of the
  driver header in a `native` test environment) so logic is testable without a board.

### Test Scope Preferences
- **Emphasis**: [To be decided — see the hardware constraint in `techContext.md` § Test Execution
  Strategy. Today the only test environment requires a connected board, which blocks unattended
  TDD. Adding a `[env:native]` for hardware-free logic tests is the recommended first move.]
- **Typical test-to-source ratio**: [To be established]
- **What is NOT typically tested**: register-level driver code that only a real peripheral can
  exercise; IDF framework boilerplate

<!-- AUTO-MANAGED: c4-architecture-start -->
## C4 Architecture

<!--
  This section is auto-managed by /bmb:c4. Run /bmb:c4 to populate or refresh.
  Until /bmb:c4 has been run for the first time, this section is a placeholder.
  Do not hand-edit between the AUTO-MANAGED markers — edits will be overwritten.
-->

C4 architecture documentation has not been generated for this project yet.

To populate this section, run `/bmb:c4`. The command builds a complete bottom-up architecture model (Code → Component → Container → Context) and stores it under `memory-bank/c4/`. After it runs, this section will contain pointers to the generated docs and guidance on how agents should use them.

`/bmb:c4` is idempotent — re-running only re-walks source directories whose content has changed. It is recommended for **large brownfield codebases** (≥3 components or ≥50 source files) and is auto-prompted at the end of `/bmb:init` for projects that meet the threshold.

<!-- AUTO-MANAGED: c4-architecture-end -->

## Recent Architecture Changes

### 2026-08-19 - Memory bank initialized
- **What Changed**: Starting conventions recorded for an empty ESP-IDF scaffold
- **Reason**: Give agents a consistent baseline before the first feature
- **Trade-offs**: Principles are proposed, not proven — revisit after the first real subsystem
- **Affected Components**: None

---

## Notes

- Keep this file updated during the BUILD phase when architectural changes occur
- Document patterns and trade-offs to help future developers understand design decisions
- Link to specific code locations where patterns are implemented
- Update diagrams to reflect current architecture
- Keep Guiding Principles current — add new principles when foundational patterns emerge
- When a new pattern appears to contradict an existing Guiding Principle, flag this explicitly
