# System Architecture Patterns

This file documents the architectural patterns, design patterns, and system structure used in this project. It helps developers understand the system's organization and maintain consistency when extending functionality.

> **Status (2026-08-19)**: Phase 3 of sensor-monitoring-dashboard landed. `lib/sensor_hub/`
> (orchestration with per-sensor failure counters and offline escalation), `lib/reading_store/`
> (FreeRTOS-mutex wrapper), and `src/sampler.c` (30-second continuous sampling task with
> watchdog scoped to the read window only) are now **built and tested**. Phases 1–3 complete
> (27/27 native tests, 21.9% RAM, 8.7% flash). Phases 4–6 (`wifi_conn`, `http_api`, web UI)
> remain **planned, not yet built**. Do not treat this file as evidence those remaining files
> exist — check `lib/`/`src/` directly.

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
Target shape (Sensor Monitoring, Hydroponic Reservoir) — Phase 1–3 components
are BUILT. Phase 4–6 components (wifi/http layers) are planned but not yet built:

┌──────────────────────────────────────────────────────────────┐
│ Application Layer (src/)                                      │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │ app_main()  ← wiring; returns after task launch         │ │
│  │ sampler.c   ← periodic reading & TWDT subscription      │ │
│  │ wifi_conn.c ← station mode, reconnect, mDNS (Phase 4)  │ │
│  │ http_api.c  ← /api/now, /api/history (Phase 5)         │ │
│  │  NOT BUILT (planned)                                     │ │
│  └─────────────────────────────────────────────────────────┘ │
└──────────────────────────────────────────────────────────────┘
         │
         └─────────────┬────────────────┬────────────────┐
                       │                │                │
        ┌──────────────▼─┐  ┌───────────▼──┐  ┌────────▼────────┐
        │ reading_store  │  │ sensor_hub   │  │ Drivers         │
        │ (mutex wrapper)│──│ (sampler     │──│ (lib/)          │
        │ (device-only)  │  │  failure     │  │ bh1750 (I2C)    │
        │ ◄─ BUILT       │  │  handling)   │  │ ds18b20 (1-Wire)│
        │    (Phase 3)   │  │ ◄─ BUILT     │  │ level_switches  │
        └────────┬────────┘  │    (Phase 3) │  │ device_status   │
                 │           └──────────────┘  │ ◄─ BUILT        │
                 │                              │    (Phase 2)    │
                 │                              └─────────────────┘
        ┌────────▼─────────────┐
        │ reading_store_core   │  ◄─── BUILT (Phase 1)
        │ (pure ring buffer,   │
        │  downsample logic;   │ ◄─── Host-testable: [env:native]
        │  NO FreeRTOS)        │
        │                      │
        │ • 2,880-entry ring   │
        │ • push/count/is_full │
        │ • downsample to      │
        │   caller buffer      │
        └──────────────────────┘
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

### Pure-Logic / Device-Only Split

**Problem**: A module that owns FreeRTOS primitives (`SemaphoreHandle_t`, task wiring, etc.) cannot
compile or run in a `[env:native]` host environment. Yet the module's core business logic (ring
buffer arithmetic, state machine, downsampling algorithm) is pure and testable off-device. How do
you make logic testable without also making it capable of running without hardware?

**Solution**: Split the module into two: a pure half with zero FreeRTOS dependency, and a thin
device-only wrapper that owns the synchronization primitive and delegates every operation to the
pure half. The pure half lives in `lib/<name>_core/` and is host-compilable; the wrapper lives in
`lib/<name>/` (or `src/`) and depends on the core.

**Implementation**:
- `lib/reading_store_core/` (`reading_store_core.h/c`) — **BUILT, Phase 1**: the ring buffer
  with static allocation, no locks, no FreeRTOS headers. All 2,880 × 20-byte entries statically
  declared. (File is short — read it directly for line-level detail rather than trusting a
  pinned line range here, since it will shift as the module grows.)
- `lib/reading_store/` (`reading_store.h/c`) — **BUILT, Phase 3**: a thin device-only wrapper
  owning a `SemaphoreHandle_t`, delegating every operation to `reading_store_core` with zero
  arithmetic of its own. The writer is the sampler task (Phase 3); the reader API supports a
  100ms timeout (reader not yet implemented — lands in Phase 5).
- `test/test_reading_store/test_reading_store.c` — **BUILT, Phase 1**: exercises
  `reading_store_core` on the host; uses `[env:native]` which links `esp_shim.h` instead of
  FreeRTOS.

**Trade-offs**:
- **Gain**: Logic testable without hardware. The 11 `reading_store_core` tests (Phase 1) run in
  CI/locally without a board attached. Downsampling, ring wrap, and count logic all have
  explicit verification.
- **Cost**: Slight verbosity — the wrapper delegates every operation (one line each). Acceptable
  because the wrapper is thin (< 50 SLOC) and the core is stable (ring arithmetic is well-known).

**When to reuse**: Whenever a module has both pure logic and FreeRTOS coupling:
- A state machine (pure) with a queue-owned task wrapper (device-only).
- A JSON serializer (pure) with an HTTP chunked-send wrapper (device-only).
- A sensor-read-history analyzer (pure) with a sampler-task wrapper (device-only).

**Scope**: This pattern is specific to embedded firmware where on-device testing requires hardware
but off-device testing of logic is feasible.

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
- **Mocking approach**: Link-time stub substitution in `[env:native]`. When `sensor_hub` depends on
  the three driver *headers* (`bh1750.h`, `ds18b20_probe.h`, `level_switches.h`), the device build
  links the real implementations; the native test environment links stub implementations from
  `test/native/stubs/` instead. No function-pointer vtables, no runtime indirection — the device
  build pays zero cost for testability. See `test/native/esp_shim.h` for the ESP-IDF type/macro
  shim that makes pure logic compilable on the host.

### Test Scope Preferences
- **Emphasis**: Unit-heavy on pure logic (implemented in `lib/<name>_core/`). Ring buffer arithmetic,
  state machines, JSON serialization, and sampling failure handling all carry defect risk and must
  have explicit coverage. Peripheral drivers (I2C register transactions, 1-Wire timing, GPIO
  polling) cannot be meaningfully tested without the physical bus and are verified manually at the
  bench instead. When a module has a pure half (per the Pure-Logic / Device-Only Split pattern),
  the core is 100% unit-tested on `[env:native]`; the wrapper is device-only and not host-tested.
- **Typical test-to-source ratio**: ~0.5 (target: ~1 test per 2 SLOC of pure logic). Phase 1 has
  11 tests for ~60 SLOC of `reading_store_core`, a ratio of 0.18; Phase 2 will add ~8 tests for
  `level_switches` (~80 SLOC); overall aim is to reach ~0.4–0.5 across all pure modules. Driver
  stubs and test harness boilerplate (esp_shim.h, stub implementations) are not counted.
- **What is NOT typically tested**: 
  - Peripheral register transactions (BH1750 I2C, DS18B20 1-Wire CRC, GPIO edge timing)
  - Wi-Fi association, reconnection backoff, mDNS registration (require a real AP)
  - `esp_http_server` request routing (framework behavior; covered by manual `curl` checks)
  - The embedded HTML/CSS/JS (no browser test harness in scope for v1)
  - FreeRTOS primitives themselves (task scheduling, semaphore fairness) — assumed correct from the IDF
  
  Compensation: Phase-specific manual hardware verification steps are documented in
  `tasks/sensor-monitoring-dashboard.md` § Per-Phase Test Guidance. A one-shot sensor read at
  boot (Phase 2), ring filling across hours (Phase 3), Wi-Fi reconnection after AP reboot (Phase 4),
  and HTTP endpoint verification with `curl` (Phase 5) are all manual checks that exercise the
  untested seams.

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

### 2026-08-19 - Phase 3: Sampler Task & Store Locking Wrapper Integrated
- **What Changed**: 
  - `lib/sensor_hub/` — orchestration module that runs one complete sample cycle across all three
    drivers (bh1750, ds18b20_probe, level_switches), with per-sensor consecutive-failure counter.
    Failure escalation: sensor marked offline after 5 consecutive failures, with counter reset on
    success. Failed reads stored with validity bit cleared, never as `0.0`.
  - `lib/reading_store/` — the thin FreeRTOS-mutex wrapper completing the Pure-Logic/Device-Only
    Split pattern. Writer (sampler task) blocks indefinitely; reader API supports 100ms timeout
    (reader not yet implemented — lands in Phase 5).
  - `src/sampler.c` — FreeRTOS task running every 30 seconds (configurable via `CONFIG_HYDRO_SAMPLE_INTERVAL_SEC`).
    Subscribes to IDF task watchdog (TWDT) **scoped to the read window only** (R1 refinement per
    creative design), never across the sleep. This resolves H1 from the plan critique: a permanent
    TWDT subscription on a 30s loop would reboot spuriously; scoping brackets it.
  - `test/native/stubs/` — link-time stub implementations of the three drivers (bh1750_stub.c,
    ds18b20_probe_stub.c, level_switches_stub.c) for host testing of `sensor_hub` failure logic
  - `test/test_sensor_hub/` — 6 new host tests covering partial/total failure, counter
    increment/reset, offline at 5th failure, offline clear on success
  - `src/Kconfig.projbuild` — new "Sampler" submenu with `CONFIG_HYDRO_SAMPLE_INTERVAL_SEC`
    (range 5–3600, default 30)
  - `src/main.c` — extended to call `reading_store_init()` then `sampler_start()` after the
    existing Phase 1/2 boot read
  - `lib/reading_store_core/include/reading_store_core.h` — fixed: guarded `esp_shim.h` include
    behind `#ifndef ESP_PLATFORM` so the header compiles cleanly in device build
- **Reason**: The sampler task is the third concurrent actor (per Decision 3, creative design).
  It reads sensors on a schedule and writes into the store; `sensor_hub` encapsulates the
  multi-driver orchestration and failure handling so the sampler itself stays thin.
- **Trade-offs**: TWDT scoping adds complexity (subscribe/unsubscribe per cycle) in exchange for
  a genuine 5-minute hang detector instead of a 30-second false-alarm generator. Failure
  escalation (offline at 5 failures) balances responsiveness against transient glitch noise.
- **Affected Components**: `lib/sensor_hub/`, `lib/reading_store/`, `src/sampler.c`,
  `src/Kconfig.projbuild`, `test/test_sensor_hub/` (new), `test/native/stubs/` (new).
  Drivers (bh1750, ds18b20_probe, level_switches, device_status) now have call sites and are
  integrated into the firmware image.

### 2026-08-19 - Phase 2: Sensor Drivers Added (bh1750, ds18b20_probe, level_switches, device_status)
- **What Changed**: Four device-driver modules now implemented and tested:
  - `lib/bh1750/` — I2C ambient-light sensor driver (0x23 address), using ESP-IDF's `i2c_master` API
  - `lib/ds18b20_probe/` — 1-Wire water-temperature probe driver, wrapping managed components
    `espressif/onewire_bus` (1.0.0) and `espressif/ds18b20` (0.3.1)
  - `lib/level_switches/` — debounced 3-band water-level state machine (FULL/MID/LOW + FAULT),
    pure-logic half of the split pattern with host test suite (`test_level_switches`)
  - `lib/device_status/` — status log-only seam (OK/SENSOR_FAULT/LEVEL_FAULT/WIFI_DOWN)
- **Reason**: These drivers enable Phase 3+ to wire sensor reads into the sampler task. The
  pure-logic `level_switches` core is verified on the host via `[env:native]`; the I2C and 1-Wire
  drivers are device-only (physical buses cannot be emulated) and verified by compile-success and
  hardware bench testing.
- **Trade-offs**: Drivers are compiled into the build and verified at image link time, but the
  application layer (`src/`) has no call sites yet — that lands in Phase 6. The device builds
  prove the drivers compile cleanly; functional integration testing is deferred to Phase 3 when
  the sampler task calls them.
- **Affected Components**: `lib/bh1750/`, `lib/ds18b20_probe/`, `lib/level_switches/`,
  `lib/device_status/`, `test/test_level_switches/` (new). The `reading_store_core` and
  `reading_store` (wrapper) are unaffected. New dependencies declared in `src/idf_component.yml`.

### 2026-08-19 - Phase 1: Pure-Logic / Device-Only Split Pattern Introduced
- **What Changed**: `lib/reading_store_core/` (pure ring buffer + downsample, no FreeRTOS) is
  now built and host-tested. It is the FIRST half of the intended Pure-Logic/Device-Only Split
  pattern; the second half (`lib/reading_store/`, a thin FreeRTOS-mutex wrapper) is **not yet
  built** — deferred until a concurrent writer (the sampler task, Phase 3+) actually needs a
  lock. This pattern enables host-testable logic in `[env:native]` while keeping the
  not-yet-written device-only concerns isolated when they do land.
- **Reason**: Prerequisite for unattended TDD in CI. A module owning FreeRTOS primitives cannot
  compile in a host environment; splitting isolates the testable arithmetic from the
  device-specific coupling. This pattern is intended to repeat for `sensor_hub` (sampler
  failure handling) and `reading_json` (HTTP chunked-send wrapper) in later phases.
- **Trade-offs**: Slight verbosity in the wrapper (delegation boilerplate), to be paid when the
  wrapper is actually written, in exchange for a host-testable core available now.
- **Affected Components**: `lib/reading_store_core/` (built), test harness
  (`test/native/esp_shim.h`, `[env:native]`, built). `lib/reading_store/` is NOT an affected
  component yet — it doesn't exist.

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
