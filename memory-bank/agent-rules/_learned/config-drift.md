---
name: Configuration Drift Between Tooling Layers
globs: ["platformio.ini", "sdkconfig*", "*.csv", "Kconfig*", "**/Kconfig.projbuild"]
paths: []
topics: ["configuration", "embedded", "esp-idf", "platformio", "build"]
priority: low
auto_generated: true
derived_from: [sensor-monitoring-dashboard]
evidence_count: 1
last_validated: 2026-08-20
---

# Configuration Drift Between Tooling Layers

When a stack has more than one configuration layer, a key set in one layer does not
necessarily reach the others. The failure is silent and the naming actively misleads.

- `board_upload.*` keys in `platformio.ini` (e.g. `board_upload.flash_size`) configure only
  the **upload tool**. They never reach the ESP-IDF build config
  (`CONFIG_ESPTOOLPY_FLASHSIZE`), which requires a matching `sdkconfig.defaults` entry.
- Whenever the board target, flash size, or partition table changes, verify the
  corresponding `sdkconfig.defaults` / Kconfig value **independently** rather than assuming
  the higher-level key propagated. Confirm against the generated `sdkconfig.<env>` file,
  not against what was written in `platformio.ini`.
- A board id chosen as "the closest available match" carries every one of its own defaults
  with it (flash size, PSRAM presence, pin mappings). Enumerate what it declares and
  override each mismatch explicitly — do not assume the differences are inert.
- Put pins, intervals, thresholds, timeouts, and platform strings (e.g. a POSIX `TZ`) in
  Kconfig, never in literals. Values a bench technician must change are exactly the ones
  that must not require a code edit.
