# System Architecture Patterns

This file documents the architectural patterns, design patterns, and system structure used in this project. It helps developers understand the system's organization and maintain consistency when extending functionality.

> **Status (2026-08-20)**: **ALL 6 PHASES COMPLETE — PROJECT FEATURE-COMPLETE FOR v1.**
> Phase 6 (web UI) just landed: `src/web/` browser assets + `embed_web_assets.py` build integration,
> with pure-logic dashboard module (`dashboard-logic.js`) + thin device-only wrapper (`app.js`).
> The project now has 54 total host-run tests (39 C native + 15 JS via Node), 32.6% RAM,
> 30.4% flash. Phases 1–6 all locked in firmware: sampler task, HTTP API, JSON serializer,
> SNTP time sync, Wi-Fi, device drivers, embedded web dashboard. Next phases (v2) would add
> pump relay control, more sensors, or time-series UI enhancements. Do not modify Phases 1–5;
> if future changes are needed, they belong in a new Phase 7+ task.
>
> Landed earlier 2026-08-20: an audit of this file against ESP-IDF v5.3.1 best practices, and
> fixes for deviations **D1–D4** (peripheral double-acquisition, `vTaskDelay` pacing, unchecked
> returns). **D5 and D6 remain open** — see § Known Deviations.

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
| **One Owner Per Peripheral** | Every bus/port/channel (I2C port, 1-Wire bus, RMT channel, timer) is created **exactly once**, by a single named owner, which passes the handle to consumers. ESP-IDF's driver APIs are paired (`i2c_new_master_bus`/`i2c_del_master_bus`, `i2c_master_bus_add_device`/`i2c_master_bus_rm_device`, `onewire_new_bus_rmt`/`onewire_bus_del`) — a module that creates and never deletes is a leak, and a second create on the same port is an **error**, not a no-op. A driver's `init()` may take a handle; it may not acquire a shared resource for itself. See § Espressif Platform Conventions. |
| Fail-Safe Defaults | A sensor read failure, network loss, or bad reading must never leave an actuator energized or the device wedged. Every failure path has a defined safe state. |
| No Blocking in `app_main` | Long-running work lives in FreeRTOS tasks with explicit stack sizes and priorities; `app_main` wires things up and returns or blocks on a supervisory primitive. |
| **Periodic Work Uses Absolute Deadlines** | A task that must run at a fixed rate uses `xTaskDelayUntil()`, never `vTaskDelay()` after the work. `vTaskDelay` makes the real period `interval + work duration`, which drifts and — worse — silently breaks any downstream logic that assumes evenly-spaced samples. If a pure module's contract depends on a constant period, that contract is stated in its header and honored by the device-only half. |
| Explicit Error Handling | Every IDF call returning `esp_err_t` is checked — including the ones whose failure is "unlikely": `esp_task_wdt_add`/`_delete`, `xTaskCreatePinnedToCore` (returns `pdPASS`), `i2c_new_master_bus`. A start/init function that can fail returns `esp_err_t`; it does not return `void` and log success unconditionally. Use `ESP_ERROR_CHECK` only where an abort is genuinely the correct response; otherwise handle and log. |
| Configuration Is Not Hard-Coded | Pin assignments, thresholds, intervals, and credentials come from Kconfig (`menuconfig`) or NVS — not string/number literals scattered through source. Secrets are never committed. **A Kconfig `range` is the guardrail, not the `help` text**: if a value is invalid or fatal on this SoC, the range must exclude it (or init must validate it), because prose in a `comment` block constrains nothing. |
| Structured Logging | Use the IDF logging macros (`ESP_LOGI`/`ESP_LOGW`/`ESP_LOGE`) with a per-module `static const char *TAG`. No `printf` for diagnostics. |

## System Architecture

### High-Level Architecture
```
Sensor Monitoring, Hydroponic Reservoir — ALL 6 PHASES COMPLETE (v1 feature-complete):

┌─────────────────────────────────────────────────────────────────────────┐
│ Browser Layer (src/web/ + embed_web_assets.py) — Phase 6 BUILT          │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │ dashboard-logic.js (pure) ◄─── BUILT (Phase 6), Host-testable    │  │
│  │ ├─ formatReadingTimestamp()    └─ test/web/ (15 Node tests)      │  │
│  │ ├─ deriveMetricBadge()                                           │  │
│  │ ├─ deriveLevelBadge()                                            │  │
│  │ ├─ isPreFirstSample()         app.js (device-only, Phase 6 BUILT)│  │
│  │ └─ buildChartSeries()         ├─ fetchNow() → /api/now           │  │
│  │                               ├─ fetchHistory() → /api/history   │  │
│  │                               └─ Delegates decisions to logic    │  │
│  │ index.html + style.css embedded via embed_web_assets.py          │  │
│  └───────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
         │
         ├─ GET / → index.html (Phase 6)
         ├─ GET /style.css, /app.js, /dashboard-logic.js (Phase 6)
         │
┌────────▼──────────────────────────────────────────────────────────────┐
│ Firmware Application Layer (src/)                                     │
│  ┌──────────────────────────────────────────────────────────────────┐│
│  │ app_main()   ← wiring; returns after task launch                ││
│  │ sampler.c    ← periodic reading & TWDT subscription (Phase 3)   ││
│  │ wifi_conn.c  ← station mode, reconnect, mDNS (Phase 4 BUILT)    ││
│  │ time_sync.c  ← SNTP client + local TZ (Phase 5 BUILT)           ││
│  │ http_api.c   ← /api/now, /api/history, / (Phase 5–6 BUILT)      ││
│  └──────────────────────────────────────────────────────────────────┘│
└─────────┬──────────────────────────────────────────────────────────────┘
          │
          ├─────────────┬────────────────┬────────────────┐
          │             │                │                │
    ┌─────▼──┐  ┌──────▼─┐  ┌───────────▼──┐  ┌────────▼─────────┐
    │reading │  │reading │  │ sensor_hub   │  │ Drivers (lib/)   │
    │_json   │  │_store  │  │ (sampler     │  │                  │
    │(pure)  │  │(mutex) │──│  failure     │  │ bh1750 (I2C)     │
    │ BUILT  │  │ BUILT  │  │  handling)   │  │ ds18b20 (1-Wire) │
    │Phase 5 │  │Phase 3 │  │ BUILT        │  │ level_switches   │
    └────────┘  └───┬────┘  │ Phase 3      │  │ device_status    │
                   │        └──────────────┘  │ BUILT Phase 2    │
                   │                          └──────────────────┘
            ┌──────▼──────────────┐
            │ reading_store_core  │
            │ (pure ring buffer,  │ BUILT (Phase 1)
            │  downsample logic;  │ Host-testable: [env:native]
            │  NO FreeRTOS)       │ 2,880-entry static buffer
            └─────────────────────┘
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
  100ms timeout (exercised in Phase 5 by http_api.c).
- `lib/reading_json/` (`reading_json.h/c`) — **BUILT, Phase 5**: pure JSON serializer with zero
  FreeRTOS/httpd dependency, taking readings from the caller and emitting JSON via a
  caller-supplied write callback. Enables streaming into `httpd_resp_send_chunk()` without
  intermediate buffering (see Phase 5 entry below). Host-tested in `test/test_reading_json/`.
- `test/test_reading_store/test_reading_store.c` — **BUILT, Phase 1**: exercises
  `reading_store_core` on the host; uses `[env:native]` which links `esp_shim.h` instead of
  FreeRTOS.

**Trade-offs**:
- **Gain**: Logic testable without hardware. The 11 `reading_store_core` tests (Phase 1) run in
  CI/locally without a board attached. Downsampling, ring wrap, and count logic all have
  explicit verification.
- **Cost**: Slight verbosity — the wrapper delegates every operation (one line each). Acceptable
  because the wrapper is thin (< 50 SLOC) and the core is stable (ring arithmetic is well-known).
- **Cost (added 2026-08-20): the split hides cross-half contracts.** The pure half can depend on a
  property only the device half can supply, and nothing checks it. Live example:
  `reading_store_core_downsample()` selects "evenly-spaced samples" **by index**, which is only
  evenly spaced *in time* if the sample period is constant — but `src/sampler.c` uses `vTaskDelay`,
  so the period varied with how long the reads took — and the host tests passed throughout,
  because the assumption was invisible to them. (Fixed 2026-08-20: the sampler now paces with
  `xTaskDelayUntil()`, and `reading_store_core_downsample()` states the constant-period
  precondition and names the sampler as the writer responsible for it.) **Rule**: when a pure
  module's correctness depends on a timing, ordering, or units property, state it in the pure
  header as an explicit precondition, and name the device-only module responsible for honoring it.

**When to reuse**: Whenever a module has both pure logic and device-only coupling (FreeRTOS, I/O, browser DOM):
- A state machine (pure) with a queue-owned task wrapper (device-only).
- A JSON serializer (pure) with an HTTP chunked-send wrapper (device-only).
- A sensor-read-history analyzer (pure) with a sampler-task wrapper (device-only).
- A dashboard interpretation engine (pure) with DOM/fetch/timer wrapper (browser-device-only). **NEW, Phase 6**: `src/web/dashboard-logic.js` (pure: timestamp formatting, badge derivation, chart series building — zero `document`, `fetch`, `setInterval`, or browser APIs) + `src/web/app.js` (device-only: all DOM wiring, network polls, element updates). The pure half is **host-testable in Node** via `test/web/dashboard-logic.test.mjs` (15 tests, zero npm dependencies); the wrapper is browser-only and verified by manual testing. This is the **first extension of the pattern to the browser layer** and the **third concrete instance** overall (after `reading_store_core`/`reading_store` in Phase 1–3 and `reading_json` in Phase 5).

**Scope**: This pattern is specific to environments where off-device testing of pure logic is feasible but on-device integration requires coupling to subsystems (FreeRTOS, hardware I/O, or browser APIs) that cannot be easily emulated. It has now proven across three domains: embedded C (ring buffer, JSON serialization, sampler wiring) and browser JavaScript (dashboard interpretation).

## Integration Patterns

### [Integration Name]
- **Type**: [To be defined]
- **Protocol**: [To be defined]
- **Direction**: [To be defined]
- **Contract**: [Schema location or documentation]

## Espressif Platform Conventions

<!--
  Added 2026-08-20 after an audit of this file against the ESP-IDF v5.3.1
  programming guide (the version pinned by platform = espressif32@6.9.0) and
  against the IDF sources under ~/.platformio/packages/framework-espidf/.
  Each rule below is here because the codebase either violated it or had no
  position on it. Cite the source when amending.
-->

These are target-specific rules that the Guiding Principles above depend on. They are ESP-IDF
facts, not project preferences — verify against the pinned IDF (5.3.1) before changing one.

### Peripheral Lifecycle

| Rule | Why (verified) |
|---|---|
| An I2C port is acquired once. A second `i2c_new_master_bus()` on the same port returns **`ESP_ERR_INVALID_STATE`** and logs `"I2C bus id(N) has already been acquired"`. | `components/esp_driver_i2c/i2c_common.c:115` in IDF 5.3.1. It is not idempotent and it does not hand back the existing bus. |
| There is **no `i2c_master_get_bus_handle()` in IDF 5.3.1** — it landed in 5.4. So a module cannot recover a handle it didn't keep. | Grepped the pinned framework tree; absent. The handle must be threaded from the owner, or the pin must move to 5.4+. |
| Each `onewire_new_bus_rmt()` allocates **1 RX + 1 TX RMT channel**; ESP32-S3 has only **4 of each**. Two buses on one GPIO both succeed — and permanently orphan half the chip's RMT capacity. | `onewire_bus_impl_rmt.c:276,290`; `soc/esp32s3/include/soc/soc_caps.h:266-268`. Silent success is what makes this worse than the I2C case, not better. |
| Devices are removed before buses are deleted (`i2c_master_bus_rm_device()` → `i2c_del_master_bus()`). | ESP-IDF I2C API reference: resources are released in the reverse of acquisition. |

### Task Watchdog (TWDT)

- **Documented mechanism is subscribe-once → `esp_task_wdt_reset()` → unsubscribe.** Calling
  `esp_task_wdt_add()`/`esp_task_wdt_delete()` once per loop iteration is not a documented
  pattern. If the intent is "watch the work, not the sleep," the idiomatic forms are (a) hold the
  subscription and `esp_task_wdt_reset()` immediately before the sleep, with
  `CONFIG_ESP_TASK_WDT_TIMEOUT_S` sized above the worst-case work window, or (b) an `esp_timer`
  one-shot armed around the work as a dedicated hang detector.
- **Know what the current config actually does.** As of this writing:
  `CONFIG_ESP_TASK_WDT_TIMEOUT_S=5` (five **seconds**) and `CONFIG_ESP_TASK_WDT_PANIC` is **not
  set** — so a timeout prints a warning plus a backtrace **and execution continues**. The TWDT is
  therefore a *detector*, not a recovery mechanism. Any doc or comment claiming it reboots or that
  it covers minutes is wrong; set `CONFIG_ESP_TASK_WDT_PANIC=y` if reboot-on-hang is the intent.
- Budget the watched window before trusting the timeout. The current sample cycle is ~1.1 s
  nominal (180 ms BH1750 conversion + ~750 ms DS18B20 12-bit conversion + 3 × 50 ms debounce) but
  reaches ~3.3 s when both I2C transactions hit their 1000 ms timeouts — inside 5 s, without much
  margin.

### Build & Flash Configuration

- **Enable core dump.** Currently `CONFIG_ESP_COREDUMP_ENABLE_TO_NONE=y` with no coredump
  partition, so an unattended crash leaves no evidence. For a headless always-on device this is
  the highest-value use of the ~12.9 MB of unallocated flash. Espressif's documented entry:
  `coredump, data, coredump, , 64K`.
- **`file(GLOB_RECURSE)` in `src/CMakeLists.txt` is a known sharp edge, not a pattern to copy.**
  Espressif recommends listing sources explicitly via `SRCS`, because "if a new source file is
  added and this method is used, then CMake won't know to automatically re-run and this file won't
  be added to the build." That is the exact failure this project already hit once — the Phase 2
  modules were silently dropped from the image (see the `lib_deps` comment in `platformio.ini`).
  The file is PlatformIO-generated; if it stays, treat "did my new file actually get compiled?" as
  a thing to verify, not assume.
- **The committed profile is a debug profile.** `CONFIG_COMPILER_OPTIMIZATION_DEBUG=y` (`-Og`) and
  `CONFIG_COMPILER_OPTIMIZATION_ASSERTION_LEVEL=2` (full assertions). Correct for the bench; for
  anything shipped, Espressif's size guide recommends `-Os` and `Silent` assertions. Define that
  second profile deliberately rather than discovering it at deploy time.

### ESP32-S3 GPIO Validity (Hosyond WROOM-1 N16R8)

`SOC_GPIO_VALID_GPIO_MASK` (`soc/esp32s3/include/soc/soc_caps.h:179`) masks out
`BIT22|BIT23|BIT24|BIT25` — **GPIO 22–25 do not physically exist on the ESP32-S3.** Combined with
26–32 (SPI flash) and 33–37 (octal PSRAM on the N16R8 part, unusable even with PSRAM disabled in
software), a bare `range 1 48` in Kconfig admits a large set of pins that cannot work. Constrain
the `range`, or validate with `GPIO_IS_VALID_GPIO()` at init and fail loudly.

### Time & Credentials

- **SNTP + local TZ now implemented (Phase 5).** `src/time_sync.c` is the SNTP client (using
  `esp_netif_sntp` API against `CONFIG_HYDRO_SNTP_SERVER`, defaulting to `pool.ntp.org`) with
  `setenv("TZ", CONFIG_HYDRO_SNTP_TZ)` + `tzset()` for local-time conversion. The sampler
  (`src/sampler.c`) stamps `time(NULL)` + `READING_VALID_TIME_BIT` into each reading, so
  `sensor_reading_t.epoch_sec` (renamed from `uptime_sec` Phase 1–4) is now wall-clock time
  anchored across restarts, not device uptime. `time_sync_is_valid()` allows callers (and
  reading_json) to distinguish "no SNTP sync yet" from "post-sync valid epoch." ✓ Decided and
  implemented.
- **Wi-Fi credentials belong in NVS, not the image.** Espressif's guidance for anything past a
  bench demo is NVS (which `esp_wifi` reads by default) or `wifi_prov_mgr`. The current
  compile-time `include/wifi_secrets.h` is correctly gitignored and guarded by `__has_include`
  (`src/wifi_conn.c:52-55`), but it bakes the credentials into the binary and satisfies neither
  branch of the "Kconfig or NVS" principle above. Note `nvs_flash_init()` is already called
  (`src/wifi_conn.c:169`), so NVS is available today. Still open — deferred to a future task.

## Known Deviations From These Patterns

<!--
  Confirmed, reproducible gaps between the patterns above and the code as
  committed. Each was verified against IDF 5.3.1 sources, not inferred. This
  list is a work queue: delete an entry when the code is fixed, do not delete
  it because it is inconvenient.
-->

Recorded 2026-08-20 against Phase 4 (`cef3419`). **D1–D4 were fixed the same day** — see § Recent
Architecture Changes → "Espressif Best-Practice Fixes D1–D4 Applied"; the fix commit is the record
of what they were. **D5 and D6 remain open.**

Also still open, from § Espressif Platform Conventions rather than this table: `uptime_sec` is
uptime rather than a real timestamp (SNTP), and Wi-Fi credentials are compiled into the image
rather than read from NVS. Both are cheapest to change before Phase 5 freezes the JSON contract.

| # | Deviation | Evidence | Impact |
|---|---|---|---|
| D5 | **Kconfig GPIO ranges admit nonexistent and fatal pins** (`range 1 48` for all four pin options). | `src/Kconfig.projbuild:9,16,27,41,50` | The `comment` block warns in prose; the `range` enforces nothing. Violates Configuration Is Not Hard-Coded. |
| D6 | **No coredump partition**, `CONFIG_ESP_COREDUMP_ENABLE_TO_NONE=y`. | `partitions.csv`; `sdkconfig.esp32-s3-devkitm-1` | An unattended crash leaves no post-mortem evidence. |

## Code Organization Patterns

<!--
  This section records SOURCE-file conventions so an agent never has to *infer*
  a new file's language or extension at build time. It is the authoritative
  anchor: when a new file is created, its extension is resolved from the table
  below, NOT from whichever file happens to sit next to it.
-->

### Source Language & Default Extensions
- **Primary language**: C (ESP-IDF) — `.c` for implementation, `.h` for headers
  - **Phase 6 Exceptions (intentional, not drift)**:
    - `src/web/*.js` — Browser-layer dashboard scripts (Phase 6). JavaScript for client-side dashboard logic.
    - `embed_web_assets.py` — PlatformIO extra_script at repo root for asset embedding. Python, required by the PlatformIO build system for compiling and embedding static assets (`src/web/*` files).
    - `test/web/*.mjs` — Node.js ES module test files for dashboard-logic.js (Phase 6). Testable via `node --test` (no npm, zero dependencies).
  - Both JavaScript and Python additions are scoped to Phase 6 and do not indicate a shift toward polyglot. The device firmware remains C-only (`src/`, `lib/`, `include/`, `test/test_*/`).
- **`.cpp`/`.hpp` permitted?**: Not currently. The device scaffold is C-only. Introducing C++ requires
  an explicit decision (it changes `idf_component_register` behaviour and linkage; C headers
  consumed from C++ need `extern "C"` guards). Record that decision here if it is made.
- **Type-checking enforced**: The compiler is the gate — `pio run` must build clean. Treat
  warnings as defects. Note this is currently aspirational: `[env:esp32-s3-devkitm-1]` has **no
  `build_flags` at all**, so "treat warnings as defects" is unenforced. Add
  `build_flags = -Wall -Wextra` to make the stated gate real. Be aware the compiler cannot catch
  the defect class that has actually bitten this project — peripheral double-acquisition and
  unchecked `esp_err_t` returns both compile clean (see § Known Deviations).

### File Extension by Directory / Role

| Directory / Role | Extension | Notes |
|------------------|-----------|-------|
| `src/` (firmware) | `.c` | Application sources; PlatformIO globs `src/*.*` into one IDF component |
| `src/web/` | `.html`, `.css`, `.js` | Browser assets (Phase 6): embedded into firmware via `embed_web_assets.py` |
| `include/` | `.h` | Headers shared across `src/` translation units |
| `lib/<name>/src/` | `.c` | Private library implementation |
| `lib/<name>/include/` | `.h` | Private library public interface |
| `test/test_<suite>/` | `.c` | Unity test suites (C, host via `[env:native]`) |
| `test/web/` | `.mjs` | Node.js ES module test suites (Phase 6): runnable with `node --test` |
| Root level | `.py` | `embed_web_assets.py` — PlatformIO extra_script for asset embedding (Phase 6) |

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

  **Cost of this seam (added 2026-08-20)** — previously recorded as an unqualified win, which
  understated it. `lib/sensor_hub` *declares* `sensor_hub_light_read`/`_temp_read`/`_level_read`
  but the **application** defines them (`src/sampler.c:147-174`). So a `lib/` module depends on
  symbols from `src/`, which means: (a) `lib/sensor_hub` cannot link standalone; (b) a missing
  definition is caught only at final link, the same late-and-opaque failure class the `lib_deps`
  belt-and-suspenders in `platformio.ini` exists to avoid; and (c) exactly one implementation can
  exist per binary. The runtime-cost claim is still true. If the seam is reused for another
  module, prefer `__attribute__((weak))` default stubs inside the `lib/` module — that keeps the
  zero-indirection property while leaving the module self-contained and linkable on its own.

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

### 2026-08-20 - Phase 6 (FINAL): Web UI Dashboard + Pure-Logic Browser Layer
- **What Changed**:
  - **Pure-Logic / Device-Only Split extended to browser**: `src/web/dashboard-logic.js` contains all
    dashboard interpretation logic (timestamp formatting with SNTP-valid gating, badge derivation,
    pre-first-sample detection, chart series building) with zero DOM/fetch/timer access, making it
    host-testable. `src/web/app.js` is the thin device-only wrapper owning all fetch calls,
    `setInterval`, and element updates, delegating every decision to dashboard-logic. This is the
    **third concrete instance** of the Pure-Logic/Device-Only Split pattern (after `reading_store_core`/
    `reading_store` and `reading_json`) and its **first application to the browser layer**.
  - **Embedded web assets**: Four files embedded in firmware image: `src/web/index.html`, `src/web/style.css`,
    `src/web/app.js`, `src/web/dashboard-logic.js`. HTTP server now serves real dashboard at `GET /`
    (was placeholder in Phase 5), plus three new routes: `GET /style.css`, `GET /app.js`,
    `GET /dashboard-logic.js`. Embedded via `embed_web_assets.py` (PlatformIO extra_script workaround).
  - **Asset embedding workaround**: Both documented PlatformIO mechanisms (`idf_component_register EMBED_TXTFILES`
    and `board_build.embed_txtfiles`) failed on `espressif32@6.9.0` + `idf-component-manager 1.x`.
    Solution: manual invocation of ESP-IDF's `data_file_embed_asm.cmake` + explicit appending to
    `LINKFLAGS` (read live at link-time, bypassing CMake DAG snapshotting). Full technical writeup
    in § Web Asset Embedding of techContext.md.
  - **Code organization exception**: `src/web/` (browser assets), `test/web/` (Node tests), and
    `embed_web_assets.py` (Python build script) are intentional exceptions to the C-only convention.
    They are scoped to Phase 6 and do not indicate a polyglot shift — the device firmware (`src/`,
    `lib/`, `include/`, `test/test_*/`) remains pure C. Recorded in § Code Organization Patterns.
- **Reason**:
  - Web UI is the final user-facing component and completes v1 feature set. Embedded assets eliminate
    external dependencies, reduce round-trips, and make the device fully self-contained (no CDN reliance,
    single HTTP response delivers the full UI).
  - Pure-Logic/Device-Only Split on the browser layer enables host-testable dashboard logic without
    DOM simulators or browser environments — 15 tests run via Node's built-in `--test` runner.
  - No new user-input surface (reuses `points` param from Phase 5). Zero npm dependencies. Accessibility
    compliant (state conveyed by text, not color alone).
- **Trade-offs**:
  - Asset embedding adds PlatformIO build-system complexity and version lock-in (workaround is
    specific to `espressif32@6.9.0` + IDF 5.3.1 + idf-component-manager 1.x). If platform is
    upgraded, re-test whether the documented mechanisms work.
  - Browser code (JavaScript) is not compiled for type safety; linting/formatting can be added in
    a future task if needed.
  - Snapshot-then-stream HTTP response pattern means dashboard snapshot is re-downsampled on every
    request; acceptable for human-facing dashboard with low polling frequency (~30s), not suitable for
    high-frequency data fetches.
- **Affected Components**: 
  - `src/web/` (new browser layer), `test/web/` (new JS tests), `embed_web_assets.py` (new build script).
  - `src/http_api.c` extended with four static-asset routes.
  - No changes to `src/main.c`, `src/sampler.c`, `lib/`, or `test/test_*/` (all Phase 1–5 components locked).
- **Verified**:
  - `node --test test/web/` — 15 tests passing (dashboard-logic pure functions).
  - `pio test -e native` — 39 tests passing (unchanged from Phase 5).
  - `pio run -e esp32-s3-devkitm-1` — SUCCESS, zero warnings. RAM 32.6% (106,692 B), flash 30.4%
    (957,040 B).
  - Clean rebuild (`rm -rf .pio/build && pio run`) verifies asset embedding and symbol linking.
  - Code review: APPROVED (one BLOCKING round fixed — stale comments claiming `board_build.embed_txtfiles`
    was working; corrected to document the actual workaround).
  - Security review: PASS. No new user-input surface, zero new dependencies, no new external integrations.
- **Scope Lock**: All 6 phases now complete. Phases 1–5 locked in firmware; no further changes to core
  functionality. Future work (Phase 7+) for v2 would be in a new task (pump relay control, additional
  sensors, time-series UI enhancements). **Project feature-complete for v1**.

### 2026-08-20 - Phase 5: HTTP API + Time Sync + JSON Serialization
- **What Changed**:
  - **HTTP API snapshot-then-stream pattern (AC-HAPPY-3 / AC-ERROR-5)**: `src/http_api.c` owns
    no sensor peripheral. Every reading served comes from `reading_store_downsample()` (Phase 3's
    locking wrapper), which already acquires the store mutex with a 100ms timeout, downsamples
    into a static ≤500-entry snapshot, and releases the lock before returning. No handler ever
    performs network I/O while holding the store lock. A lock-acquire timeout (`ESP_ERR_TIMEOUT`)
    maps to HTTP 503 (Service Unavailable). Handlers serve `/` (placeholder page), `/api/now`
    (single reading), and `/api/history?points=` (parallel arrays, clamped 1–500, default 180).
  - **Pure-Logic/Device-Only Split extended (reading_json)**: `lib/reading_json/` is the pure
    JSON serializer — zero FreeRTOS, zero httpd headers, zero dynamic allocation. Takes readings
    and emits JSON through a caller-supplied write callback, enabling `src/http_api.c` (the
    device-only caller) to stream each fragment straight into `httpd_resp_send_chunk()` without
    intermediate buffering. Host-tested in `[env:native]`. This is the second concrete instance
    of the Pure-Logic/Device-Only Split pattern applied (first was reading_store_core in Phase 1).
  - **SNTP + local time (src/time_sync.c)**: SNTP client using `esp_netif_sntp` (modern API,
    pinned IDF 5.3.1) against configurable server (Kconfig `CONFIG_HYDRO_SNTP_SERVER`, default
    `pool.ntp.org`). Local-time conversion via `setenv("TZ", CONFIG_HYDRO_SNTP_TZ)` +
    `tzset()` for day/night/DST calculations. `time_sync_is_valid()` reports whether an SNTP
    sync has completed (device has no battery-backed RTC; 1970-anchored epoch means "never synced").
  - **epoch_sec struct field rename + READING_VALID_TIME_BIT**: `sensor_reading_t.uptime_sec`
    (Phases 1–4: device uptime via `esp_timer_get_time()`) renamed to `epoch_sec` (Phase 5+:
    wall-clock Unix time from `time(NULL)`). Zero RAM/capacity impact (same uint32_t width).
    New validity bit `READING_VALID_TIME_BIT` (bit 3) set by sampler on `time_sync_is_valid()`;
    cleared otherwise. reading_json respect this bit: if clear, `"time_valid": false` and
    consumers know epoch_sec is untrustworthy.
  - New Kconfig "SNTP / Time" submenu: `CONFIG_HYDRO_SNTP_SERVER` (string), `CONFIG_HYDRO_SNTP_TZ`
    (POSIX TZ string, placeholder default with help text noting it is not the real timezone).
  - `src/main.c` extended: `time_sync_start()` wired after `wifi_conn_start()`, then
    `http_api_start()` wired after `time_sync_start()`. Both checked; failures escalate via
    `device_status` seam (`http_api_start()` reuses `DEVICE_STATUS_WIFI_DOWN` as the closest
    existing failure seam for "dashboard delivery channel down").
  - `platformio.ini` updated: `reading_json` added to `lib_deps` and `test_filter` (host tests).
- **Reason**: HTTP API completes the three-actor concurrent pattern (sampler task Phase 3,
  Wi-Fi event loop Phase 4, HTTP request dispatcher Phase 5), with JSON serialization pure and
  testable on the host, and time sync independent of Wi-Fi connectivity semantically
  (though functionally it requires it). Snapshot-then-stream is the concrete implementation of
  AC-HAPPY-3 and AC-ERROR-5 (store lock timeout safety).
- **Trade-offs**: HTTP request handlers must each allocate their own 10 KB snapshot buffer (POINTS_MAX * sizeof(sensor_reading_t)) on the stack;
  the HTTP server task's stack size must be verified to comfortably exceed this (default IDF task stack
  multipliers must be checked against actual link, not assumed). Snapshot is re-downsampled on every
  request rather than cached; acceptable for a human-facing dashboard where request frequency is
  low (sub-second requests would be UI polling, not typical).
- **Affected Components**: `lib/reading_json/` (new), `src/http_api.c` (new), `src/time_sync.c`
  (new), `src/main.c`, `src/Kconfig.projbuild`, `lib/reading_store_core/` (doc-only for
  epoch_sec rename), `test/test_reading_json/` (new, 6 tests). Extends
  Pure-Logic/Device-Only Split pattern (second instance: reading_json + http_api).
- **Verified**: 39/39 native tests pass (`pio test -e native`); `pio run -e esp32-s3-devkitm-1`
  SUCCESS, 0 warnings, RAM 32.6% (106,692 B), flash 29.9% (941,620 B). Manual hardware
  verification: curl to `/api/now` and `/api/history` endpoints, time_valid bit behavior across
  SNTP sync boundary (documented in tasks/sensor-monitoring-dashboard.md § Per-Phase Test Guidance).

### 2026-08-20 - Espressif Best-Practice Fixes D1–D4 Applied (peripheral ownership, task pacing, error checking)
- **What Changed**:
  - **Single peripheral ownership (D1, D2)**. `src/sampler.c` is now the sole owner of every sensor
    peripheral — the I2C master bus, the 1-Wire RMT bus, and the level-switch GPIO configuration —
    acquired exactly once in the new `sampler_sensors_init()`. `app_main()` creates nothing: its
    boot read goes through the same `sensor_hub_*_read()` seams the sampler task uses. `src/main.c`
    consequently lost its `i2c_bus_create()`/`level_gpio_configure()` helpers, its three
    `read_*_once()` functions, its polarity-normalizing macros, and its
    `driver/i2c_master.h`/`driver/gpio.h`/`bh1750.h`/`ds18b20_probe.h` includes.
    `sampler_sensors_init()` also rejects a second call rather than re-acquiring.
  - **Absolute-deadline pacing (D3)**. The sampler loop uses `xTaskDelayUntil()` instead of a
    trailing `vTaskDelay()`, and logs a warning if a cycle ever overruns the interval.
    `reading_store_core_downsample()` now documents its constant-period precondition explicitly
    and names the sampler as the writer responsible for it.
  - **Checked returns (D4)**. `sampler_start()` returns `esp_err_t` and validates
    `xTaskCreatePinnedToCore` against `pdPASS`; `app_main()` escalates a failure to
    `DEVICE_STATUS_SENSOR_FAULT` instead of logging "sampler started" over a nonexistent task.
    Both `esp_task_wdt_add`/`_delete` are checked.
  - Removed the stale "open item for Phase 3 hardware verification" comment in `src/sampler.c` —
    the question was answerable from the IDF source without a board, and the answer was "it fails".
  - Corrected the TWDT comment in the sampler loop to state what the current config actually does
    (5 s timeout, no panic → detector, not recovery).
- **Reason**: D1 was not latent. The duplicate `I2C_NUM_0` acquire returned
  `ESP_ERR_INVALID_STATE`, so `s_light_ready` stayed false and **the BH1750 produced no valid
  reading from the sampler on any boot** — the lux series was entirely invalid bits, and Phase 5
  was about to build a chart on top of it. D2 silently orphaned an RMT TX+RX pair out of the S3's
  four. D3 broke the downsampler's even-spacing precondition, and D4 could hide a total sampling
  failure behind a success log.
- **Trade-offs**: `app_main()` no longer independently exercises the drivers, so the boot read is
  no longer an *independent* check of the sensor path — it now shares all wiring with the sampler.
  Accepted: that shared wiring is the point, and a boot read that passed while the sampler's path
  was broken is exactly the false comfort D1 produced. Also, moving ownership into `sampler.c`
  means the module now does two jobs (peripheral ownership + periodic sampling); if a second
  consumer of these buses appears (Phase 5's `/api/now` reading live rather than from the ring),
  ownership should move out to its own `sensors` module rather than growing `sampler.c` further.
- **Verified**: `pio test -e native` 31/31 pass; `pio run -e esp32-s3-devkitm-1` SUCCESS, zero
  warnings, RAM 29.1%, flash 27.6%. Post-fix grep confirms exactly one call site each for
  `i2c_new_master_bus`, `gpio_config`, `bh1750_init`, `ds18b20_probe_init` — all in `sampler.c`.
  **Not yet bench-verified**: D1's fix is the one with observable behavior change, so the
  confirmation is watching two consecutive sampler cycles log plausible lux on real hardware.
- **Affected Components**: `src/main.c`, `src/sampler.c`, `include/sampler.h`,
  `lib/reading_store_core/include/reading_store_core.h` (doc-only). D5 and D6 remain open in
  § Known Deviations.

### 2026-08-20 - Espressif Best-Practice Audit of This File (docs only, no code change)
- **What Changed**: Audited the Guiding Principles and patterns in this file against the ESP-IDF
  v5.3.1 programming guide (the version pinned by `platform = espressif32@6.9.0`) and against the
  IDF sources in `~/.platformio/packages/framework-espidf/`. Resulting edits:
  - Added two Guiding Principles — **One Owner Per Peripheral** and **Periodic Work Uses Absolute
    Deadlines** — and tightened **Explicit Error Handling** (name the specific calls that were
    going unchecked) and **Configuration Is Not Hard-Coded** (a Kconfig `range` must be the
    guardrail, not the `help` text).
  - Added § **Espressif Platform Conventions**: peripheral lifecycle, TWDT, build/flash config,
    ESP32-S3 GPIO validity, time & credentials. Every rule carries its verification source.
  - Added § **Known Deviations From These Patterns** (D1–D6): confirmed, reproducible gaps
    between these patterns and the code as committed, each verified against IDF 5.3.1 rather than
    inferred. Intended as a work queue for a follow-up task.
  - **Corrected a false claim** in the Phase 3 entry below: the TWDT scoping was described as
    buying "a genuine 5-minute hang detector." The timeout is 5 **seconds**
    (`CONFIG_ESP_TASK_WDT_TIMEOUT_S=5`) and `CONFIG_ESP_TASK_WDT_PANIC` is not set, so it warns
    and continues rather than rebooting.
  - Recorded two previously-unstated **costs**: the Pure-Logic/Device-Only Split can hide
    cross-half contracts (the downsampler's even-spacing assumption vs. `vTaskDelay`), and the
    link-time stub seam inverts the `lib/` → `src/` dependency direction.
- **Reason**: The principles were sound and Espressif-idiomatic but governed peripheral *access*
  while saying nothing about peripheral *lifetime and ownership* — and that gap had already
  produced two live defects (D1, D2), one of which silently disables a sensor for the life of
  every boot. A patterns file that reads as satisfied while the code is broken is worse than
  no patterns file.
- **Trade-offs**: This file gets longer and more prescriptive, which is a real cost for a
  progressive-discovery memory bank. Accepted because the added rules are each tied to a concrete
  defect or an SoC fact rather than to taste. § Known Deviations deliberately carries an
  expiry: entries are deleted when fixed, so if it stops shrinking, that itself is the signal.
- **Affected Components**: None — documentation only. No `src/` or `lib/` file was modified;
  D1–D6 remain open in the code and need a separate build/fix task.
- **What Changed**:
  - `lib/wifi_backoff/` — pure-logic module computing capped exponential backoff delay sequence
    (1→2→4→8→16→30s, per AC-ERROR-3), with zero FreeRTOS/ESP-IDF dependency. Host-testable,
    4 new unit tests.
  - `src/wifi_conn.c` — device-only Wi-Fi station mode manager: event-driven reconnect using
    `wifi_backoff` + single `esp_timer` one-shot; mDNS hostname `hydroponics` registered on
    every `IP_EVENT_STA_GOT_IP` (re-register on reconnect); serial-logged IP on every
    connect/reconnect (AC-ERROR-7 fallback for unreliable `.local` resolution).
  - `src/main.c` — extended: `wifi_conn_start()` called after sampler task init; sampling
    independent of connectivity.
  - `src/idf_component.yml` — added `espressif/mdns ^1.2` managed dependency.
- **Reason**: Wi-Fi connectivity completes the second concurrent actor pattern (Phase 3 introduced
  the sampler task; Phase 4 adds the Wi-Fi event loop running in IDF's internal task), with
  backoff logic pure and testable and device-specific state machine isolated in a thin wrapper.
- **Trade-offs**: Backoff module duplication (separate from generic `esp_timer` backoff patterns)
  justified by AC-ERROR-3's specific requirement (cap at 30s, not 2^N without bound). The single
  `esp_timer` one-shot (not a dedicated task) minimizes overhead; it re-arms itself on each
  `WIFI_EVENT_STA_DISCONNECTED`.
- **Affected Components**: `lib/wifi_backoff/`, `src/wifi_conn.c`, `src/main.c`,
  `src/idf_component.yml`. Extends the Pure-Logic / Device-Only Split pattern: backoff
  calculation is testable on `[env:native]` independently of Wi-Fi hardware.

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
  a hang detector that does not false-alarm on the sleep. Failure escalation (offline at 5
  failures) balances responsiveness against transient glitch noise.
  > **Correction (2026-08-20)**: this entry originally claimed the scoping bought "a genuine
  > 5-minute hang detector instead of a 30-second false-alarm generator." That was wrong on both
  > counts. `CONFIG_ESP_TASK_WDT_TIMEOUT_S=5` is five **seconds**, not minutes, and
  > `CONFIG_ESP_TASK_WDT_PANIC` is **not set** — so a timeout prints a warning and a backtrace and
  > then keeps running. It never reboots. The scoping is still the right call (a permanent
  > subscription really would trip on every sleep), but what it buys is a 5-second *detector*, not
  > recovery. See § Espressif Platform Conventions → Task Watchdog.
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
