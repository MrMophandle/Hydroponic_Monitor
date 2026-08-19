# Technology Context

This file documents the technology stack, infrastructure, and tooling used in this project. It serves as a reference for understanding technical decisions and helps maintain consistency across development phases.

> **Status (2026-08-19)**: Phase 2 complete. Sensor drivers (`bh1750`, `ds18b20_probe`,
> `level_switches`, `device_status`) implemented and tested. Managed IDF components
> (`espressif/onewire_bus`, `espressif/ds18b20`) declared in `src/idf_component.yml`.
> Host-testable logic tests expanded to 21 passing (`pio test -e native` covers
> `test_reading_store` + `test_level_switches`). Device build (`pio run -e esp32-s3-devkitm-1`)
> compiles clean at 4.0% RAM, 6.6% flash. Drivers are **not yet called from `src/`** — that
> lands in Phase 6; Phase 2 proves they compile and link correctly against ESP-IDF/FreeRTOS
> headers.

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
# Build the default environment (esp32-s3-devkitm-1)
pio run

# Explicit environment
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
# Run logic tests on the host (no hardware required)
pio test -e native

# Verbose host test output
pio test -e native -v

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
- [To be determined] — the IDF component set available in `sdkconfig` includes `esp_wifi`,
  `mqtt`, `esp_http_client`, `esp_http_server`, and `esp-tls`, so Wi-Fi + MQTT/HTTP are
  all buildable without adding dependencies.

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

## Recent Technology Changes

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
