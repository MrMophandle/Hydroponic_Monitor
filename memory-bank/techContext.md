# Technology Context

This file documents the technology stack, infrastructure, and tooling used in this project. It serves as a reference for understanding technical decisions and helps maintain consistency across development phases.

> **Status (2026-08-19)**: greenfield. The repository is a freshly generated PlatformIO + ESP-IDF
> scaffold — `src/main.c` contains only an empty `app_main()`. Everything below that is marked
> `[To be determined]` should be filled in as the first features land.

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
# Run the PlatformIO test suite on-device
pio test -e esp32-s3-devkitm-1

# Verbose test output
pio test -e esp32-s3-devkitm-1 -v
```

> **Note**: `test/` currently holds only the generated README — there are no tests yet. The first
> `/bmb:build` task must establish the Unity test harness before TDD can be enforced. On-device
> testing requires the board to be connected; a native test environment can be added to
> `platformio.ini` for host-runnable logic tests if hardware-free unit testing is wanted.

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
- [x] Tests require special environment setup — **hardware**: an ESP32-S3 DevKitM-1 must be
      connected over USB for the default environment. This means the test suite is **not**
      runnable in CI or by an unattended agent as configured today.

### Test Discovery
- [x] Tests are automatically discovered — PlatformIO collects test suites from `test/`,
      one subdirectory per suite (`test/test_<name>/`)
- Naming: `test/test_<suite>/test_<suite>.c` with Unity `setUp`/`tearDown`/`RUN_TEST`

### Test Isolation
- [x] Tests must run sequentially — PlatformIO flashes and runs one test suite at a time on
      the device

### Environment variables for testing
```bash
# None defined yet
```

## Technology Stack

### Runtime Environment
- **Target MCU**: ESP32-S3 (Xtensa LX7), board `esp32-s3-devkitm-1`
- **RTOS**: FreeRTOS (bundled with ESP-IDF)
- **Bootloader / partitioning**: ESP-IDF defaults; see `sdkconfig.esp32-s3-devkitm-1`

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
- **Pin versions**: `platformio.ini` currently pins neither the `espressif32` platform version nor
  any library versions. Pin `platform = espressif32@<version>` before the build needs to be
  reproducible.
