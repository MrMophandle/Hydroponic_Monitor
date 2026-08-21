# Technology Context

This file documents the technology stack, infrastructure, and tooling used in this project. It serves as a reference for understanding technical decisions and helps maintain consistency across development phases.

> **Status (2026-08-20)**: **Phase 6 FINAL PHASE complete (sensor-monitoring-dashboard).** All 6 phases of sensor-monitoring-dashboard
> now built. Web UI layer (`src/web/` dashboard assets + `embed_web_assets.py` build integration)
> implemented and tested. 54 total host-run tests: 39 native C tests (`pio test -e native`: 
> 11 reading_store + 10 level_switches + 6 sensor_hub + 4 wifi_backoff + 6 reading_json + 2 
> reading_store time_valid) + 15 new JS tests (`node --test test/web/*.test.mjs` for dashboard-logic.js).
> Device build (`pio run -e esp32-s3-devkitm-1`): SUCCESS at 32.6% RAM (106,692 B), 30.4% flash
> (957,040 B); 0 warnings. Security review PASS (no new user input, zero new dependencies). 
> Code review APPROVED (comment-only fixes to stale docs in Phase 6, behavior unchanged).
> **Project feature-complete for v1: all 6 phases locked into firmware image.**
>
> **Status (2026-08-21)**: **onboard-status-led Phase 2 COMPLETE.** Extended `lib/device_status/` 
> with reachability-fact tracking (`status_report_wifi()`, `status_report_http()`, `status_snapshot()`) 
> + host-testable via ESP_PLATFORM/esp_shim.h pattern. Native test count: 71 tests
> (`pio test -e native`: 11 reading_store + 10 level_switches + 6 sensor_hub + 4 wifi_backoff + 
> 6 reading_json + 2 reading_store time_valid + 24 status_led_core + 8 device_status). Device build 
> unchanged (32.6% RAM, 30.6% flash; Phase 2 is additive-only). Code review APPROVED.
>
> **Status (2026-08-21)**: **onboard-status-led Phase 3 COMPLETE (FINAL PHASE).** WS2812 RGB LED 
> driver and tick task implementation: new `lib/status_led/` module (RMT TX driver + vendored 
> `led_strip_encoder` from ESP-IDF example, Apache-2.0, unmodified), plus `status_led_task` 
> 100 ms tick at FreeRTOS priority 2 with transmit-only-on-change + suppressed-error-count logging.
> Kconfig additions: `HYDRO_STATUS_LED_ENABLE`, `HYDRO_STATUS_LED_GPIO` choice (GPIO 48/47/38), 
> `HYDRO_STATUS_LED_BRIGHTNESS`, `HYDRO_STATUS_LED_BLINK_MS`. Native tests unchanged: 71/71 PASS.
> Device build: SUCCESS at 32.6% RAM (106,708 B), 30.7% flash (965,632 B); 10 LOW warnings 
> (9 pre-existing + 1 cppcheck false positive on unused-function). Code review APPROVED 
> (recommended error-path cleanup applied). RMT peripheral budget: 1 TX + 1 RX of 4 channels 
> reserved for status LED; 3 TX + 3 RX remain for future features (DS18B20 1-Wire uses 1 TX + 1 RX).

## Component Structure

### Components/Modules

```
Firmware Application:
- Path: src/
- Language: C (ESP-IDF)
- Source extensions: .c for implementation, .h for headers
- Test Directory: test/
- Test Framework: Unity (via PlatformIO Test Runner)
```

Supporting directories:

```
Project Headers:
- Path: include/
- Purpose: headers shared across src/ translation units

Project Libraries:
- Path: lib/
- Purpose: project-private libraries; one subdirectory per library, each with its own
  src/ + include/. PlatformIO auto-detects and builds these, and the include path is
  exposed to src/ automatically.
```

### Shared/Common Code
- Location: `include/` (project-wide headers), `lib/<name>/` (self-contained libraries)
- Purpose: [To be determined — no shared code exists yet]

## Development Commands

All commands assume the PlatformIO CLI (`pio`) is on PATH. If only the VS Code extension is
installed, use `~/.platformio/penv/bin/pio` or the PlatformIO toolbar.

### Building
```bash
# Build the firmware. ALWAYS pass -e: bare `pio run` builds every environment,
# including [env:native], which is a host TEST environment and is not buildable
# with `pio run` — it has no ESP-IDF framework, so compiling src/ into it fails.
# `pio test -e native` is the correct way to exercise that environment.
pio run -e esp32-s3-devkitm-1

# Clean build artifacts
pio run --target clean
```

### Flashing & Monitoring
```bash
# Build + upload to the attached board
pio run --target upload

# Serial monitor
pio device monitor

# Upload then monitor in one step
pio run --target upload --target monitor

# List attached devices (to find the port)
pio device list
```

### Configuration (Kconfig / menuconfig)
```bash
# ESP-IDF project configuration; writes sdkconfig.esp32-s3-devkitm-1
pio run --target menuconfig
```

### Testing
```bash
# Run C logic tests on the host (no hardware required)
pio test -e native

# Verbose host test output for C tests
pio test -e native -v

# Run JavaScript tests (dashboard-logic.js pure functions, Phase 6+)
# NOTE: pass the glob, not the directory. `node --test test/web/` fails with
# MODULE_NOT_FOUND on Node 24 (verified on v24.13.1) — Node tries to execute the
# directory as a module instead of discovering tests inside it.
node --test test/web/*.test.mjs

# Run the PlatformIO test suite on-device (ESP32-S3 board must be connected)
pio test -e esp32-s3-devkitm-1

# Verbose device test output
pio test -e esp32-s3-devkitm-1 -v
```

> **Note**: Pure-logic modules (ring buffer, state machines, JSON serialization) are tested on
> the host via `[env:native]`. This environment compiles with `test/native/esp_shim.h` (ESP-IDF
> type/macro shims) and stubs from `test/native/stubs/` for driver headers, allowing 100% of pure
> logic to be verified without a board.
> 
> Peripheral drivers and device-specific behavior (I2C register timing, Wi-Fi association, HTTP
> chunked streaming) require the physical board and are tested on `esp32-s3-devkitm-1` (or
> verified manually at the bench). Per-phase hardware verification steps are documented in
> `tasks/sensor-monitoring-dashboard.md` § Per-Phase Test Guidance.

### Linting / Static Analysis
```bash
# PlatformIO's built-in static analysis (cppcheck by default)
pio check -e esp32-s3-devkitm-1
```
[No linter or formatter is configured yet — clang-format / clang-tidy config to be decided.]

**Known false positive — cppcheck `__has_include` evaluation (Phase 4+)**:
Cppcheck's own `__has_include` evaluation fails to resolve `wifi_secrets.h` under `pio check`'s
fixed-configuration invocation, even though the correct `include/` search path is present in
cppcheck's `-I`/`--includes-file` list and the real `pio run` build always finds the file. This
is a cppcheck quirk, not a code defect — the `#if !__has_include("wifi_secrets.h")` guard in
`src/wifi_conn.c` works correctly. A scoped suppression is added to `platformio.ini`:
`check_flags = --suppress=preprocessorErrorDirective:*/wifi_conn.c`. This prevents the false
positive from cluttering the check output without hiding real issues.

### Type Checking
Not applicable (C — the compiler is the type check; `pio run` is the gate).

## Test Execution Strategy

The single home for how this project's tests run.

### Test Running Strategy
- [x] Always run from project root (`pio test` resolves `platformio.ini` from there)
- [ ] Can run per-component safely
- [x] **Pure logic tests are now runnable in CI/unattended**: `pio test -e native` compiles and
      runs on the host without any hardware. All ring buffer, state machine, and serialization
      tests execute here.
- [x] **Device-specific tests require hardware**: `pio test -e esp32-s3-devkitm-1` requires an
      ESP32-S3 board connected over USB. Currently unused (Phase 1 has no device tests); will be
      used for Wi-Fi, HTTP, and timing-dependent features in later phases.

### Test Discovery
- [x] Tests are automatically discovered — PlatformIO collects test suites from `test/`,
      one subdirectory per suite (`test/test_<name>/`)
- Naming convention: `test/test_<suite>/test_<suite>.c` with Unity `setUp`/`tearDown`/`RUN_TEST`
- **Host-testable logic**: Tests in `test/test_<name>/test_<name>.c` that exercise pure modules
  (those with no FreeRTOS includes) are auto-discovered for `[env:native]` and run on the host.
- **Device tests** (future phases): Tests that depend on Wi-Fi, I2C, 1-Wire, or other peripherals
  will be placed in `test/device/` or marked with `#ifdef DEVICE_BUILD` guards if in the same file.

### Test Isolation
- [x] **Host tests** (native): Run sequentially on the host, one suite at a time
- [x] **Device tests** (when added): Will run sequentially on the device, one suite per flash cycle

### Environment variables for testing
```bash
# None defined yet
```

## Technology Stack

### Runtime Environment
- **Target MCU**: ESP32-S3 (Xtensa LX7), board **`esp32-s3-devkitc-1`** (standing in for the actual
  hardware: Hosyond ESP32-S3-WROOM-1 N16R8 — 16 MB flash, 8 MB octal PSRAM)
  - **Why `devkitc-1`**: PlatformIO's board database has no entry for the N16R8 variant. The
    `devkitc-1` is the closest upstream match; `platformio.ini` overrides are used to configure
    the actual 16 MB flash and custom partition table. (See `board_upload.flash_size=16MB` and
    `board_build.partitions = partitions.csv` in `platformio.ini`.)
  - **PSRAM not used**: 8 MB octal PSRAM is present but disabled in software (GPIO 33–37 avoided).
    A 58 KB ring buffer does not need it; disabling avoids configuration faults. Deferred to v2
    if needed for larger history or a GUI.
  - **Physical header pinout is NOT the reference board's.** The PlatformIO board id is a
    stand-in; where a GPIO physically emerges is a property of the Hosyond PCB. Trust the
    board's silkscreen, or confirm empirically — never a reference-board pinout diagram. This
    board has already been documented wrong once: the onboard status LED is on **GPIO 48**
    (vendor Q&A), while the vendor's own documentation cites GPIO 47. Bench-confirmed
    2026-08-21.
  - **Bench-confirmed pin positions** (add rows as they are verified; a GPIO number matching
    its header position is coincidence, **not** a general mapping — the 40-pin header cannot
    map 1:1 onto GPIO numbers reaching 48, and 3V3/GND/EN occupy positions):

    | GPIO | Physical header pin | Confirmed | Method |
    |------|---------------------|-----------|--------|
    | 4 (`CONFIG_HYDRO_DS18B20_GPIO`) | pin 4 | 2026-08-21 | 3 s HIGH / 3 s LOW toggle firmware, located with a multimeter |

  - **Pin-finder technique** (for locating any GPIO on this board): temporarily replace
    `app_main()`'s body with a `gpio_config()` + infinite `gpio_set_level()` loop at 3 s
    HIGH / 3 s LOW, flash, and probe the header for a pin alternating steady 3.3 V and
    steady 0 V. Use 3 s rather than sub-second — an averaging multimeter cannot settle on a
    fast square wave and reports a meaningless mid-scale value (~1.65 V). Run the loop
    **first** in `app_main()` and never return, so `sampler_sensors_init()` cannot hand the
    pin to a bus driver mid-test. **The status LED is not a marker for a diagnostic build**:
    the WS2812 latches its last received frame, so it holds whatever colour production
    firmware last wrote. Identify the build by flash size instead (~213 KB diagnostic vs
    ~966 KB production).
- **Platform**: `espressif32@6.9.0` (ESP-IDF 5.3.1, has `i2c_master` API required by BH1750 driver)
- **RTOS**: FreeRTOS (bundled with ESP-IDF)
- **Bootloader / partitioning**: Custom partition table in `partitions.csv` (16 MB flash, no OTA)
  configured via `board_build.partitions` override

### Languages & Frameworks
- **C**: ESP-IDF application code (`src/main.c` — `app_main()` entrypoint)
- **ESP-IDF**: framework, pinned by the PlatformIO `espressif32` platform version
- **CMake** ≥ 3.16: build system (`CMakeLists.txt` includes `$ENV{IDF_PATH}/tools/cmake/project.cmake`)
- **Ninja**: generator used by the IDF/CMake build

### Data Layer
- **NVS (Non-Volatile Storage)**: available via ESP-IDF `nvs_flash` for persisted config
  [not yet used]
- **SPIFFS / FATFS**: available in the IDF component set [not yet used]

### API & Communication
- **HTTP Server** (`esp_http_server`, Phase 5–6) — `src/http_api.c` serves six endpoints:
  - **GET `/`** — Embedded HTML dashboard (real dashboard HTML page, Phase 6; replaces Phase 5 placeholder)
  - **GET `/style.css`** — Embedded CSS stylesheet (src/web/style.css, Phase 6)
  - **GET `/app.js`** — Embedded browser application script (src/web/app.js, Phase 6)
  - **GET `/dashboard-logic.js`** — Embedded pure-logic module (src/web/dashboard-logic.js, Phase 6)
  - **GET `/api/now`** — Single most-recent reading in JSON format (`{"t":<epoch>,"time_valid":<bool>,"lux":<float|null>,"temp_c":<float|null>,"level":"<FULL|MID|LOW|FAULT|UNKNOWN>","valid":{...}}`)
  - **GET `/api/history?points=<N>`** — Parallel arrays of N readings (t, time_valid, lux, temp_c, level), clamped 1–500, default 180
  - Implementation: snapshot-then-stream pattern with `reading_store_downsample()` acquiring the store lock once, releasing before HTTP chunked-send (no lock held during I/O). Static assets are embedded in the firmware image via `embed_web_assets.py` (see § Web Asset Embedding below)

### Infrastructure & Deployment
- **Deployment**: physical flash over USB (`pio run -t upload`)
- **CI/CD**: none configured
- **OTA**: `esp_https_ota` is available in the IDF component set [not configured]

### Development Tools
- **Build orchestration**: PlatformIO Core (`platformio.ini`)
- **IDE**: VS Code + `platformio.platformio-ide` (see `.vscode/extensions.json`; the C/C++
  extension pack is explicitly listed as unwanted in favour of PlatformIO's IntelliSense)
- **Static analysis**: `pio check` (cppcheck)
- **Test framework**: Unity, via PlatformIO Test Runner

### Managed IDF Components (Phase 2+)
- **espressif/onewire_bus** (^1.0.0) — Generic 1-Wire bus driver using RMT hardware backend.
  Required by `lib/ds18b20_probe/`. Declared in `src/idf_component.yml`.
- **espressif/ds18b20** (^0.3.1) — DS18B20 temperature probe driver. Required by
  `lib/ds18b20_probe/`. Pinned to 0.3.x (not 0.4.0) due to a version-solver incompatibility
  with the bundled `idf-component-manager` 1.x in `espidf@6.9.0` (0.4.0's manifest uses a
  `$CONFIG{...}` conditional that 1.x cannot parse). See inline comment in `src/idf_component.yml`.
- **espressif/mdns** (^1.2) — mDNS hostname registration and resolution. Required by `src/wifi_conn.c`
  to register `hydroponics.local` on each Wi-Fi connection. Resolves to 1.11.3 as of Phase 4.
  Declared in `src/idf_component.yml`.

### External Services
- [None yet]

## Key Configuration Files

| File | Purpose | Hand-edited? |
|------|---------|--------------|
| `platformio.ini` | Board, platform, framework, build flags, library deps | Yes |
| `CMakeLists.txt` | Top-level IDF project definition (`project(HydroponicMonitor)`) | Rarely |
| `src/CMakeLists.txt` | PlatformIO-generated component registration; globs `src/*.*` | No — regenerated |
| `sdkconfig.esp32-s3-devkitm-1` | Generated ESP-IDF Kconfig | No — use `menuconfig` |

<!-- AUTO-MANAGED: c4-references-start -->
## C4 References

<!--
  This section is auto-managed by /bmb:c4. Run /bmb:c4 to populate or refresh.
  Until /bmb:c4 has been run for the first time, this section is a placeholder.
  Do not hand-edit between the AUTO-MANAGED markers — edits will be overwritten.
-->

C4 architecture documentation has not been generated for this project yet.

After `/bmb:c4` runs, this section will contain pointers to the Container-level diagram and per-container detail docs (the C4 model places technology details at the Container level). This file (`techContext.md`) will remain the canonical source for tech versions, component file paths, and development commands; the C4 docs reference but do not duplicate them.

<!-- AUTO-MANAGED: c4-references-end -->

## Web Asset Embedding (Phase 6 Build System Finding)

### Background: PlatformIO Embed-Txtfiles Mechanisms

PlatformIO documents two mechanisms for embedding text/binary files (HTML, CSS, JS) into the firmware image:

1. **`idf_component_register(... EMBED_TXTFILES ...)`** — ESP-IDF's component-level embedding via CMake. Generates `_binary_<name>_start`/`_end` symbols.
2. **`board_build.embed_txtfiles`** — PlatformIO's project-level option (platform = espidf).

**Both mechanisms were attempted and failed** on this project's pinned version (`platform = espressif32@6.9.0`, ESP-IDF 5.3.1, idf-component-manager 1.x):

| Mechanism | Failure Mode | Evidence |
|-----------|--------------|----------|
| `idf_component_register EMBED_TXTFILES` | "source not found" compile-time error | The IDF CMake custom command runs (generates `.S` assembly), but PlatformIO's espidf integration for non-"main" components never invokes it. |
| `board_build.embed_txtfiles` | "undefined reference" linker error | Generates the `.S` assembly and registers an ordering dependency, but never compiles the `.S` to an object file or adds it to the link. |

### Solution: Manual ESP-IDF Script Invocation

**File**: `embed_web_assets.py` (PlatformIO extra_script, added to `platformio.ini`)

The workaround manually:
1. Invokes ESP-IDF's `data_file_embed_asm.cmake` script directly (same script both broken mechanisms use internally)
2. Compiles the resulting `.S` assembly to an object file (`.o`)
3. Appends the object file path to `LINKFLAGS` (read live at link-time, not snapshotted by the CMake DAG)
4. Adds an explicit `env.Depends()` dependency on the firmware ELF to guarantee the object is rebuilt when needed

**Why this approach succeeds where the others fail:**
- `PIOBUILDFILES` and CMake's DAG are snapshotted at build-configuration time. Objects added to either after that point silently disappear at link time.
- `LINKFLAGS` is read live by the linker's command-line generator at build-execution time, so appending to it after configuration guarantees the linker sees the path.
- The explicit dependency ensures SCons correctly orders the build steps.

**Verification**: Clean rebuild (`rm -rf .pio/build && pio run`) links successfully with zero warnings. The `_binary_*` symbols are defined and accessible to `src/http_api.c`.

### Lessons for Future Maintenance

- If `espressif32` platform is upgraded, re-test whether the two documented mechanisms work in the new IDF version. If so, this workaround can be removed.
- If assets are added/removed, update the `WEB_ASSETS` list in `embed_web_assets.py` and the corresponding `extern` declarations in `include/http_api.h`.
- The workaround is specific to the `espidf` framework under PlatformIO on this version and is not a portable general-purpose pattern.

## Recent Technology Changes

### 2026-08-21 - onboard-status-led Phase 3 (FINAL): WS2812 RGB LED Driver & Tick Task
- **What Changed**:
  - **New `lib/status_led/` module** (RMT WS2812 device-only driver): `status_led_init()` creates exactly one RMT TX channel; `status_led_show()` transmits a `status_led_rgb_t` frame (GRB byte order, 3 bytes). The module knows nothing about LED policy (which state to show) — that is entirely `lib/status_led_core`'s pure-logic job. Device-only: not compiled under `[env:native]` (no RMT peripheral on the host).
  - **Vendored `led_strip_encoder`** (ESP-IDF example, Apache-2.0, unmodified): `lib/status_led/{include,src}/led_strip_encoder.{h,c}` copied verbatim from Espressif's `examples/peripherals/rmt/led_strip/main/led_strip_encoder.{c,h}` (v5.3.1 example tag). Handles WS2812 bit-level timing: 10 MHz RMT resolution, T0H/T0L/T1H/T1L tick counts. Pinned at commit-level via git history for reproducibility (not managed via `idf_component.yml`).
  - **RMT memory configuration**: `mem_block_symbols = 48` (verified legal minimum on ESP32-S3 per `esp_driver_rmt/src/rmt_tx.c`'s `SOC_RMT_MEM_WORDS_PER_CHANNEL = 48`). One WS2812 pixel (3 bytes) consumes exactly 1 of 4 RMT memory blocks instead of 2 (the vendored example's 64-symbol buffer would consume 2), leaving 3 blocks for the DS18B20 1-Wire bus TX+RX pair and headroom.
  - **New `status_led_task` module** (device-only FreeRTOS tick task): 100 ms tick period (fixed `#define`, not Kconfig). Task reads reachability facts from `device_status` (set by Phase 2), calls `status_led_core_derive_state()` to decide LED state, calls `status_led_core_frame()` to compute RGB + blink animation (tick counter + Kconfig `HYDRO_STATUS_LED_BLINK_MS`), and transmits via `status_led_show()`. Transmit-only-on-change (no redundant RMT activity when LED is steady). Suppressed-error-count logging (AC-ERROR-2): first `status_led_show()` failure logged immediately; consecutive failures counted but not logged; recovery logged with suppression count. Task priority: 2 (one above FreeRTOS main, so LED animates during boot before blocking sensor reads).
  - **Kconfig integration**: New `src/Kconfig.projbuild` "Status LED (onboard RGB)" submenu with four options:
    - `CONFIG_HYDRO_STATUS_LED_ENABLE` (bool, default y): gate to disable LED entirely (escape hatch if GPIO wrong or absent).
    - `CONFIG_HYDRO_STATUS_LED_GPIO` (choice): GPIO 48 (vendor Q&A), 47 (third-party docs), 38 (fallback). Resolution is a single if/else-if ladder in `status_led.c`, not buried in a plain int+range (AC-VERIFY-6 pattern: Kconfig `choice`, not `int` + `range`, makes the decision visible and greppable).
    - `CONFIG_HYDRO_STATUS_LED_BRIGHTNESS` (int, 0–255, default 128): scalar multiplied into R/G/B in `status_led_core_frame()` for user dim-down.
    - `CONFIG_HYDRO_STATUS_LED_BLINK_MS` (int, 100–1000 ms, default 300): blink half-period (on time and off time, so full blink cycle is 2x this). Rounding: periods not a multiple of 100 ms tick silently round down (documented in Kconfig help text and in `status_led_task.c` code comment).
  - **Kconfig-gated disable**: If `CONFIG_HYDRO_STATUS_LED_ENABLE` is false at compile time, `status_led_start()` returns `ESP_OK` immediately (no RMT acquire, no task create, no error logged — a supported configuration, not a degraded state, per AC-VERIFY-8). Pure Kconfig guard (`#ifndef CONFIG_HYDRO_STATUS_LED_ENABLE`), not runtime bool.
  - **Error path cleanup**: `status_led_init()` error paths properly clean up (added during code review): `rmt_del_channel()` if `rmt_new_led_strip_encoder()` fails, plus `s_led_encoder->del()` and `rmt_del_channel()` if `rmt_enable()` fails. Per AC-ERROR-1 (non-fatal boot errors), `status_led_start()` failure does not abort boot.
  - **No new user input surface**: LED state is read-only from `device_status` facts (wifi/http reachability). No HTTP API changes.
  - **RMT peripheral budget**: RMT has 4 TX channels and 4 RX channels on ESP32-S3. Phase 3 claims 1 TX for the status LED. Phase 2's DS18B20 1-Wire uses 1 TX + 1 RX (owned by `lib/ds18b20_probe`). This leaves 2 TX + 3 RX (net 2 TX for future use + 1 RX spare). RMT contention would manifest as a non-fatal boot error from `rmt_new_tx_channel()` in `status_led_init()` (documented behavior).
- **Reason**:
  - Ambient hardware indicator (onboard RGB LED) communicates device status without HTTP or UI. Useful for offline operation (no Wi-Fi) and at-a-glance system health while interacting with other systems.
  - Device-only RMT driver decouples firmware timing details (WS2812 bit encoding) from policy (which color means what). `lib/status_led_core` remains pure logic, testable on the host, leaving the driver as a thin hardware-specific shim. Matches Phase 2's `lib/device_status` Pure-Logic/Device-Only Split pattern.
  - Kconfig `choice` for GPIO (not int+range) documents the architectural decision: the GPIO is genuinely ambiguous per vendor/third-party docs, and the choice ensures the decision is visible in config and greppable in code.
  - Task priority 2 (above main) ensures LED animates during the slow sensor-read sequence at boot, user-visible feedback that the device is alive.
  - Transmit-only-on-change + suppressed-error-count logging keeps RMT quiet and logs bounded even if the LED driver fails.
- **Impact**:
  - `pio test -e native`: 71/71 PASS (unchanged — Phase 3 adds zero host tests by design; RMT and FreeRTOS are device-only).
  - `pio run -e esp32-s3-devkitm-1`: SUCCESS at 32.6% RAM (106,708 B, unchanged), 30.7% flash (965,632 B, +0.1% vs Phase 2). Firmware image includes the status-LED driver, vendored encoder, Kconfig menu, task code, and all the main.c wiring.
  - `pio check -e esp32-s3-devkitm-1`: 10 LOW warnings total (9 pre-existing + 1 new cppcheck `unusedFunction` false positive on `status_led_start` — same class as the 9 pre-existing ones on functions that ARE called cross-file; confirmed called at `src/main.c:74`). Not a real defect.
  - Code review: APPROVED with 1 RECOMMENDED finding applied (error-path cleanup above) and 1 OPTIONAL finding left as-is (stack-size comment precision, already caveated as bench-unconfirmed).
  - Security review: PASS (no user input, no network surface, no persisted data touched).
  - `platformio.ini`: added `status_led` library to `[env:esp32-s3-devkitm-1]` `lib_deps` list.
  - **Bench Verification Pending** (human-only): physical LED observation (colors match state), HTTP failure injection (LED response to reachability loss), DS18B20-while-blinking (RMT/1-Wire coexistence). See `memory-bank/tasks/onboard-status-led.md` § Per-Phase Test Guidance, Phase 3 for the documented procedure.
- **Migration Notes**: Fresh checkouts should run `pio run -e esp32-s3-devkitm-1` to compile the new driver. `pio menuconfig` now shows "Hydroponic Monitor" → "Status LED (onboard RGB)" submenu for the four new options (or `cat sdkconfig.esp32-s3-devkitm-1 | grep CONFIG_HYDRO_STATUS_LED`). Default Kconfig values are reasonable (LED enabled, GPIO 48, half-brightness, 300 ms blink half-period). No pin-conflict migration needed (GPIO 48 is explicitly guarded in the Kconfig help text as "may differ per board revision").

### 2026-08-20 - Phase 6 (FINAL): Web UI Dashboard + Embedded Assets
- **What Changed**:
  - **Browser-side Pure-Logic / Device-Only Split**: `src/web/dashboard-logic.js` (pure, zero DOM/fetch/timer) handles all interpretation logic: timestamp formatting (with SNTP-valid gating), badge derivation (metric live/offline, level state), pre-first-sample detection, and chart series building. Host-tested in `test/web/dashboard-logic.test.mjs` via Node's built-in `--test` runner (15 tests, zero npm dependencies). `src/web/app.js` owns all DOM wiring, `fetch()` calls, `setInterval()`, and element updates — a thin device-only wrapper that delegates every decision to `DashboardLogic`.
  - **Embedded Web Assets**: Four static files embedded in firmware image via `embed_web_assets.py` (PlatformIO extra_script): `src/web/index.html`, `src/web/style.css`, `src/web/app.js`, `src/web/dashboard-logic.js`. The HTTP server (`src/http_api.c`) now serves real dashboard at `GET /` (was placeholder in Phase 5) plus three new asset routes: `GET /style.css`, `GET /app.js`, `GET /dashboard-logic.js`. All four assets embedded as `_binary_*_start/_end` symbols (ESP-IDF convention) and linked into firmware image.
  - **Asset Embedding Workaround**: Neither PlatformIO's documented embed mechanisms (`idf_component_register EMBED_TXTFILES` or `board_build.embed_txtfiles`) worked on `espressif32@6.9.0` with `idf-component-manager 1.x`. Solution: `embed_web_assets.py` manually invokes ESP-IDF's `data_file_embed_asm.cmake` script, compiles the result to object files, and appends them to `LINKFLAGS` (read live at link time, not snapshotted by CMake DAG). See § Web Asset Embedding for full technical writeup and future-maintenance notes.
  - **No new user-input surface**: Dashboard reuses the same `points` query parameter from Phase 5's `/api/history` route. No new credentials, API keys, or configuration surfaces added.
  - **Zero npm dependencies**: The 15 new JS tests run via Node's built-in `node --test` (ES modules), no package.json, no `npm install` required. Test file: `test/web/dashboard-logic.test.mjs`.
  - **Accessibility compliance**: State (level badges, metric badges, timestamps) conveyed by literal text (`FULL`, `MID`, `LOW`, `FAULT`, `live`, `offline`, `UNKNOWN`), never color alone. Per productBrief.md accessibility NFR.
- **Reason**:
  - Web UI is the final user-facing component. Pure-Logic/Device-Only Split applied to browser layer enables host-testable dashboard logic without a browser environment or DOM simulator.
  - Embedded assets mean the device is fully self-contained: no external CSS/JS fetches (no dependency on a CDN), single HTTP response delivers all assets (no round-trips), zero additional flash cost over the 4 KB HTML + 3 KB CSS + 4 KB app.js + 4 KB dashboard-logic.js (total ~15 KB uncompressed in source; embedded overhead minimal).
  - Asset embedding workaround is a one-time investment specific to this PlatformIO/IDF version combo; documented for future maintenance.
- **Impact**:
  - `pio test -e native` unchanged: still 39 C tests (no new C functionality).
  - `node --test test/web/*.test.mjs` new: 15 JS tests for dashboard-logic (pure functions). No CI integration yet; manually invoked with `node --test`. Pass the glob, not the directory — the directory form fails on Node 24.
  - Device RAM/flash: 32.6% RAM (106,692 B), 30.4% flash (957,040 B); +0.5% flash vs Phase 5 (asset embedding overhead). Total 54 host-run tests across project (39 C + 15 JS).
  - Device build verified with clean rebuild (`rm -rf .pio/build && pio run`): 0 warnings, link succeeds, symbols accessible to http_api.c.
  - Code review: one BLOCKING round (stale/self-contradictory comments in CMakeLists.txt and http_api.c claiming `board_build.embed_txtfiles` was the working mechanism when it was actually abandoned). Fixed directly (comment-only, zero behavior change), re-verified build clean. Final verdict: APPROVED.
  - Security review: PASS. No new user-input surface, zero new dependencies, no new external system integrations.
- **Migration Notes**:
  - Fresh checkouts must run `node --test test/web/*.test.mjs` to verify new JS tests pass (only requires Node.js; works in CI).
  - Manual browser verification: `curl http://192.168.x.x/` returns the full HTML dashboard; `curl http://192.168.x.x/api/now | jq` confirms JSON endpoint still works. Open browser to `http://hydroponics.local/` (or IP) to see rendered dashboard polling `/api/now` and `/api/history` every ~30 seconds.
  - This phase marks **feature-complete for v1**: all 6 phases implemented, tested, and committed. Phases 1–5 remain unchanged and locked. Future work (Phase 7+) for v2 would include pump relay control, more granular time-series options, or additional sensors.

### 2026-08-20 - Phase 5: SNTP + HTTP Server + JSON Serialization
- **What Changed**:
  - New SNTP client: `src/time_sync.c` uses `esp_netif_sntp` (modern IDF 5.3.1 API) to sync
    wall-clock time from `CONFIG_HYDRO_SNTP_SERVER` (Kconfig, default `pool.ntp.org`). Local
    time conversion via `setenv("TZ", CONFIG_HYDRO_SNTP_TZ)` + `tzset()` for day/night/DST logic.
  - New HTTP server endpoints via `src/http_api.c` (`esp_http_server` wrapper):
    - GET `/` — placeholder dashboard
    - GET `/api/now` — current reading in JSON
    - GET `/api/history?points=<N>` — N historical readings (clamped 1–500, default 180)
  - New JSON serializer `lib/reading_json/` (pure logic, zero FreeRTOS/httpd) emits readings
    via caller-supplied callback (enables streaming into `httpd_resp_send_chunk()` with no
    intermediate buffer overhead). Respects validity bits: invalid sensors serialize as `null`.
  - New Kconfig "SNTP / Time" submenu: `CONFIG_HYDRO_SNTP_SERVER` (string, default `pool.ntp.org`),
    `CONFIG_HYDRO_SNTP_TZ` (POSIX TZ string, placeholder default).
  - `lib/reading_store_core.h`: named validity-bit constants added (`READING_VALID_*_BIT`),
    replacing raw hex literals. New `READING_VALID_TIME_BIT` (bit 3) for SNTP-sync state.
    `sensor_reading_t.epoch_sec` now holds wall-clock seconds (was `uptime_sec`), semantically
    changed from device uptime to Unix time.
  - `src/sampler.c`: stamps `time(NULL)` + `time_sync_is_valid()`-derived `READING_VALID_TIME_BIT`
    instead of `esp_timer_get_time()`. Peripheral ownership and TWDT scoping unchanged.
  - `src/main.c`: `time_sync_start()` → `http_api_start()` wired into boot sequence, after
    `wifi_conn_start()`. Both checked; failures escalate via `device_status` seam.
  - `platformio.ini`:
    - `[env:esp32-s3-devkitm-1]` — `lib_deps` extended: `reading_json` added to force compile-time
      link verification (though no application integration yet).
    - `[env:native]` — `test_filter` extended: `test_reading_json` added (6 new host tests for
      JSON serialization).
  - `test/test_reading_store/test_reading_store.c`: +2 tests for `time_valid` bit set/clear,
    mixed valid/invalid-time entries (13 tests now, was 11).
  - No new managed IDF components this phase (SNTP is built-in to `esp_netif`, HTTP server is
    built-in `esp_http_server`).
- **Reason**:
  - Wall-clock time replaces device uptime so readings persist across reboots and can be
    correlated with external clocks. Day/night scheduling (planned pump relay feature) requires
    local time, not UTC.
  - HTTP API is the user-facing dashboard backend; snapshot-then-stream pattern with lock
    timeout → HTTP 503 ensures reading-store contention never causes network hangs (AC-HAPPY-3
    / AC-ERROR-5).
  - Pure-logic JSON serializer is the second instance of Pure-Logic/Device-Only Split pattern,
    enabling host tests without httpd, and callback-based streaming without intermediate buffers.
- **Impact**:
  - `pio test -e native` now runs 39 tests total (was 31; +6 reading_json, +2 reading_store).
  - Device RAM/flash: 32.6% RAM (106,692 B), 29.9% flash (941,620 B); +3.5% RAM, +2.4% flash
    vs. Phase 4 (HTTP server, JSON serializer, SNTP, time tracking).
  - HTTP endpoints reachable once Wi-Fi AP is reachable; real time-of-day accurate once first
    SNTP sync completes.
- **Migration Notes**:
  - Fresh checkouts must run `pio test -e native` to verify new host tests pass.
  - Kconfig defaults set; `menuconfig` can override server + TZ if needed.
  - Manual bench verification: `curl http://192.168.x.x/api/now` and
    `curl http://192.168.x.x/api/history` before/after AP reboot to confirm time_valid state.

### 2026-08-20 - Phase 4: mDNS managed component and cppcheck suppression added
- **What Changed**:
  - New managed IDF component `espressif/mdns ^1.2` added to `src/idf_component.yml`, required by
    `src/wifi_conn.c` for registering `hydroponics.local` on every Wi-Fi connection. Resolves to
    1.11.3.
  - New scoped cppcheck suppression added to `platformio.ini`:
    `check_flags = --suppress=preprocessorErrorDirective:*/wifi_conn.c`. This suppresses a
    verified false positive where cppcheck's own `__has_include` evaluation fails under `pio check`'s
    fixed-configuration invocation, even though the code correctly guards `#include "wifi_secrets.h"`
    and the real build always finds the file. Not a defect; documented in Linting/Static Analysis
    section for future reference.
  - `[env:native]` `test_filter` extended: added `test_wifi_backoff` to the host test list
    (alongside existing `test_reading_store`, `test_level_switches`, `test_sensor_hub`).
  - `[env:esp32-s3-devkitm-1]` `lib_deps` extended: added `wifi_backoff` to device-environment
    library dependencies.
- **Reason**:
  - mDNS is the canonical path for the bookmark (`http://hydroponics.local/`); re-registration on
    every reconnect (per AC-ENTRY-2) keeps it valid across DHCP lease changes.
  - cppcheck false positive is a known cppcheck limitation with `__has_include` in fixed-config
    mode; the suppression prevents spurious failures in CI without hiding real issues.
  - Host tests for `wifi_backoff` pure logic extend CI coverage; device build continues to use the
    actual Wi-Fi stack.
- **Impact**:
  - `pio run` now pulls `espressif/mdns` into the build; dependency is locked in `dependencies.lock`.
  - `pio check` no longer reports spurious preprocessor errors for `wifi_conn.c`.
  - `pio test -e native` now runs 31 tests total (4 new `test_wifi_backoff` added).
  - Device RAM/flash impact: 29.1% RAM (95,236 B), 27.5% flash (866,484 B) as of phase-end.
- **Migration Notes**: Fresh checkouts must run `pio run` to fetch `espressif/mdns`. No breaking
  changes; the suppression is transparent to development workflows.

### 2026-08-19 - Phase 3: Kconfig option and test library path added
- **What Changed**:
  - New `src/Kconfig.projbuild` "Sampler" submenu added with `CONFIG_HYDRO_SAMPLE_INTERVAL_SEC`
    (range 5–3600, default 30). Sampler task period is now Kconfig-configurable, not hardcoded.
  - New `platformio.ini` line: `lib_extra_dirs = test/native` in `[env:native]` section.
    PlatformIO's test runner now auto-discovers stub libraries from `test/native/stubs/`
    (bh1750_stub.c, ds18b20_probe_stub.c, level_switches_stub.c) for host linking; device build
    ignores this path and links the real implementations from `lib/`.
  - Extended `[env:native]` `test_filter`: added `test_sensor_hub` to the list alongside existing
    `test_reading_store` and `test_level_switches`. All three test suites now run together on the
    host without a board.
- **Reason**:
  - Sampler interval is a tuning parameter — making it Kconfig-configurable rather than a literal
    allows different deployments to choose responsiveness vs. power trade-offs without code changes.
  - Stub libraries in `test/native/stubs/` enable host testing of higher-level orchestration
    (`sensor_hub`, later `reading_json`) without the physical peripherals. Link-time stub
    substitution (not function pointers) means zero runtime cost in the device build.
  - `test_sensor_hub` extends test coverage to the failure-handling logic and offline escalation.
- **Impact**:
  - `pio menuconfig` now shows "Hydroponic Monitor" → "Sampler" submenu with the new option
  - `pio test -e native` now runs 27 tests (11 reading_store + 10 level_switches + 6 sensor_hub)
  - Device builds continue to use real drivers from `lib/` — no build breakage; `lib_extra_dirs`
    is `[env:native]`-scoped and ignored by the device environment
- **Migration Notes**: No migration needed; the stub path and test filter are backward-compatible.
  `test_sensor_hub` is a new test suite and will have zero tests if not present (will not fail
  the test run). If the stub library is missing, the native test build will fail with linker
  errors — the error is clear and ties back to the `lib_extra_dirs` addition if you grep for it.

### 2026-08-19 - Phase 2: Managed IDF components and device-driver libraries added
- **What Changed**:
  - New `src/idf_component.yml` declares managed ESP-IDF components: `espressif/onewire_bus`
    (^1.0.0) and `espressif/ds18b20` (^0.3.1). The `idf-component-manager` now resolves and
    locks these into `dependencies.lock`.
  - New `platformio.ini` `[env:esp32-s3-devkitm-1]` section: `lib_deps = bh1750,
    ds18b20_probe, device_status` forces compilation of driver libraries even though no
    call sites exist yet (Phase 6 integration). This proves drivers compile cleanly before
    their first use in application code.
  - New `[env:native]` section: `test_filter = test_reading_store, test_level_switches`
    expands host test coverage to include the level-switches debounce state machine.
- **Reason**:
  - Phase 2 implements four device drivers; two of them (`bh1750` I2C, `ds18b20` 1-Wire) rely
    on managed ESP-IDF components for the underlying bus abstractions.
  - Forcing compilation now (via `lib_deps`) uncovers build issues early, rather than
    discovering them later when the drivers are first called in Phase 6.
  - Adding `test_level_switches` to the host test suite extends unattended CI coverage;
    `pio test -e native` now runs 21 tests (11 from Phase 1 + 10 new).
- **Impact**:
  - `pio run -e esp32-s3-devkitm-1` now includes driver code in the image (flash/RAM impact:
    4.0% RAM, 6.6% flash as of phase-end verification).
  - `pio test -e native` gains 10 new tests; all 21 pass on the host without hardware.
  - `dependencies.lock` is now a tracked file (records resolved managed-component versions
    for reproducible builds).
- **Migration Notes**: Fresh checkouts must run `pio run -e esp32-s3-devkitm-1` to fetch
  and lock managed components. `dependencies.lock` should be committed to git. A `.gitignore`
  update adds `managed_components/` (build artifact).

### 2026-08-19 - Phase 1: Platform pinned, board corrected, native test environment added
- **What Changed**:
  - `platformio.ini`: Platform pinned to `espressif32@6.9.0` (ESP-IDF 5.3.1); board retargeted to
    `esp32-s3-devkitc-1` with `board_upload.flash_size=16MB` and custom partition table override
  - New `[env:native]` environment for host-based logic testing (no hardware required)
  - `test/native/esp_shim.h`: ESP-IDF type/macro shims (`esp_err_t`, `ESP_OK`, no-op `ESP_LOG*`)
  - `partitions.csv`: 16 MB flash partition table (nvs/phy_init/factory; no OTA)
- **Reason**:
  - Platform pin ensures reproducible IDF version and `i2c_master` API availability for BH1750
  - Board correction aligns configuration with actual hardware (N16R8: 16 MB flash, 8 MB PSRAM)
  - Native environment enables unattended TDD — pure logic tests run on the host without a board
- **Impact**:
  - `pio run` now builds for N16R8 config (was misconfigured for 8 MB variant)
  - `pio test -e native` runs 11 logic tests today (Phase 1's `test_reading_store` suite),
    in CI/locally without hardware; the Test Strategy's ~31-test target is spread across all
    six phases as later suites (`test_level_switches`, `test_sensor_hub`, `test_reading_json`)
    are added — not all present yet
  - `pio test -e esp32-s3-devkitm-1` still requires a board (used for device-specific features in later phases)
- **Migration Notes**: Fresh checkouts must run `pio test -e native` to verify the host environment.
  Flashing to hardware uses the corrected board config: `pio run --target upload`

### 2026-08-19 - Initial scaffold recorded
- **What Changed**: Memory bank initialized against the generated PlatformIO + ESP-IDF scaffold
- **Reason**: Baseline for all subsequent Banyan workflows
- **Impact**: None on the codebase — documentation only
- **Migration Notes**: N/A

---

## Notes

- Keep this file updated during the BUILD phase when new technologies are introduced
- Document the "why" behind technology choices, not just the "what"
- Include version numbers and configuration locations for maintainability
- Update Component Structure section when adding new modules or services
- Update Development Commands section when commands change
- **Pin versions**: `platformio.ini` pins `platform = espressif32@6.9.0` as of Phase 1
  (2026-08-19). Library dependencies declared via `idf_component.yml` (Phase 2+) still need
  their own version pins when added.
