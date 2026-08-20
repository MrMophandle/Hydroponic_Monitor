# Archive: Hydroponic Sensor Monitoring Dashboard

## Metadata
- Task: `sensor-monitoring-dashboard`
- Complexity: Level 4
- Started: 2026-08-19
- Completed: 2026-08-20
- Duration: 2 calendar days; ~11.9 h of agent wall time across 7 sessions
- Roadmap Link: `memory-bank/roadmap/sensor-monitoring-dashboard.md`
- Feature branch: `feature/sensor-monitoring-dashboard` (17 commits)

## Executive Summary

Took a repository whose entire firmware was an empty `app_main()` and delivered a
complete ESP32-S3 monitoring device in six phases: three sensor drivers, a
watchdog-scoped sampler task, a mutex-guarded in-RAM ring buffer, Wi-Fi station mode with
backoff reconnect and mDNS, SNTP-derived local wall-clock time, an HTTP JSON API, and an
embedded browser dashboard. Every phase shipped with a clean device build (0 compiler
warnings throughout) and a growing host-run test suite that reached **54 tests with zero
hardware attached**.

**Status qualifier — read this before trusting the "complete" label.** Every verification
in this archive is a host test or a clean cross-compile. As of the final phase commit, the
firmware had **never been confirmed against real sensors**. The one bench event that has
occurred since (first upload attempt, no sensors) is recorded under *Deployment* below and
found a real defect — the serial console was unreadable at the default monitor baud. The
bench-verification punch list in *Future Considerations* is the honest remaining scope.

## System Overview

### Purpose
Answer one question for one person — the repo owner, a hobbyist running a single
hydroponic reservoir: *"I can bookmark a page on my browser that shows me how the
hydroponic system is doing."* That sentence is the whole product spec, and the mDNS
hostname exists specifically because a changing DHCP IP would break the bookmark, which is
the single stated success metric.

### Scope
**In**: three sensors (ambient light, water temperature, three-band water level), 24 h of
in-RAM history, a LAN-hosted dashboard, two JSON endpoints.

**Out, deliberately**: MQTT, cloud, companion app, authentication, OTA, and persistence
across reboot. Relay/pump control is deferred to a separate roadmap feature — v1 actuates
nothing. The feature leaves two seams for it: a `status_set()` reporting hook and a
fail-safe `FAULT` level state a future pump interlock can consume.

### Key Capabilities
- Ambient light in lux (BH1750 over I2C, address `0x23`)
- Water temperature in °C (DS18B20 over 1-Wire)
- Water level as an ordered three-band state plus a diagnostic fourth: `FULL` / `MID` /
  `LOW` / `FAULT`, from two float switches
- 2,880-entry ring buffer (~58 KB, statically allocated, 30 s interval = 24 h)
- `GET /` dashboard, `GET /api/now`, `GET /api/history?points=N`
- Per-sensor offline escalation, `FAULT` reporting, watchdog-backed hang recovery

## Architecture

### Overview
A single ESP32-S3 firmware image with four concurrent concerns: a sampler task owning
every sensor peripheral, a lock-guarded ring store, an `esp_http_server` instance, and the
Wi-Fi/mDNS/SNTP connectivity stack. `app_main()` stays thin — it performs one boot read,
then starts the sampler, Wi-Fi, time sync, and HTTP API in that order and returns.

The organizing principle is the **Pure-Logic / Device-Only Split** (see *Design
Decisions*), which is why `lib/` contains both pure host-testable modules and thin
device-only wrappers rather than one module per concern.

### Component Diagram

```
                       ┌──────────────────────────┐
  browser ── HTTP ────▶│  http_api.c              │
  (LAN, mDNS)          │  esp_http_server         │
                       │  / /api/now /api/history │
                       └───────┬──────────────────┘
                               │ reads (snapshot under lock, stream after release)
                       ┌───────▼──────────────────┐
                       │  reading_store           │  device-only: owns the mutex
                       │   └▶ reading_store_core  │  PURE: ring math + downsample
                       └───────▲──────────────────┘
                               │ pushes every 30 s
                       ┌───────┴──────────────────┐
                       │  sampler.c               │  owns ALL sensor peripherals
                       │   └▶ sensor_hub          │  failure escalation (5 → offline)
                       └───────┬──────────────────┘
                    ┌──────────┼──────────┐
              ┌─────▼────┐ ┌───▼────────┐ ┌▼───────────────┐
              │ bh1750   │ │ ds18b20_   │ │ level_switches │ PURE: debounce
              │ (I2C)    │ │ probe (1W) │ │ (GPIO 5/6)     │ + 4-state table
              └──────────┘ └────────────┘ └────────────────┘

  wifi_conn.c ──▶ wifi_backoff (PURE)      time_sync.c ──▶ SNTP + TZ/tzset()
  device_status.c — status_set() seam, log-only in v1 (LED consumer deferred)
```

### Data Flow
1. `sampler` task wakes every 30 s (`CONFIG_HYDRO_SAMPLE_INTERVAL_SEC`), subscribes to the
   task watchdog, reads all three sensors, unsubscribes, sleeps. The watchdog brackets
   **only the read window** — never the sleep — so a hung I2C transaction reboots the
   device but a normal idle interval does not.
2. Each reading is stamped with epoch seconds plus a `time_valid` bit and pushed into
   `reading_store`, which takes the mutex and delegates all buffer arithmetic to
   `reading_store_core`. A failed sensor read is stored with its `valid` bit **cleared**,
   never as `0.0`.
3. `http_api` downsamples under the lock into a bounded snapshot buffer, releases the lock,
   then chunk-streams the JSON. A 100 ms lock-acquire timeout returns HTTP 503 rather than
   blocking the server task.
4. `app.js` polls `/api/now` and `/api/history`, and computes wall-clock chart labels in
   the browser from the device's reported epoch plus the browser's own clock.

### Integration Points
- **Wi-Fi AP / LAN**: station mode only; credentials from `include/wifi_secrets.h`
  (gitignored, template at `wifi_secrets.h.example`)
- **mDNS**: registers `hydroponics.local`, re-registered on every reconnect
- **SNTP**: server + POSIX `TZ` string, both Kconfig values
- **No other external system.** No cloud, no broker, no telemetry egress.

## Design Decisions

### D1: Pure-Logic / Device-Only Split
- **Decision**: split any module that needs a platform primitive (a FreeRTOS mutex, a DOM
  handle) at the point the primitive is introduced. The pure half carries the tests; the
  thin wrapper carries the primitive and holds no logic of its own.
- **Rationale**: `projectConfig.md` flagged at init that the only test environment targeted
  `esp32-s3-devkitm-1`, so `pio test` required a connected board — blocking unattended TDD.
  This split is what removed that blocker.
- **Alternatives considered**: a runtime vtable for driver substitution (rejected: on-device
  indirection cost for a test-only benefit); one `reading_store` module owning both buffer
  and mutex (the original design, caught at plan critique as finding H2 — it cannot compile
  under `[env:native]` at all).
- **Outcome**: rediscovered independently **four** times — `reading_store_core`,
  `wifi_backoff`, `reading_json`, and `dashboard-logic.js` in the browser. This is the
  single highest-value reusable artifact the task produced.
- **Reference**: `memory-bank/creative/sensor-monitoring-dashboard-design.md` Decision 4;
  `systemPatterns.md` § Pure-Logic/Device-Only Split.

### D2: Snapshot-then-stream for `/api/history`
- **Decision**: downsample under the lock into a bounded buffer, release the lock, then
  chunk-stream.
- **Rationale**: resolved a genuine three-way collision between chunked streaming, the
  100 ms lock timeout, and index alignment — holding the lock across a chunked response
  would stall the sampler for the duration of a slow client.
- **Alternatives considered**: streaming under the lock (rejected: sampler starvation);
  copying the entire 58 KB ring (rejected: memory).
- **Outcome**: the 100 ms timeout → 503 path had zero callers between Phase 3 and Phase 5
  and worked correctly the first time it was exercised.

### D3: `epoch_sec` as `uint32_t`
- **Decision**: reuse the existing `uint32_t uptime_sec` field for epoch seconds and steal
  one spare bit of the `valid` bitfield for `time_valid`.
- **Rationale**: zero RAM change, zero ring-capacity change, and the JSON contract was
  being frozen in the same phase — a later change would have been breaking rather than
  additive.
- **Trade-off accepted**: wraps in 2106. Chosen for byte budget, not for correctness-for-
  all-time.

### D4: Wall-clock time pulled into Phase 5 (mid-build scope widening, user-approved)
- **Decision**: add SNTP + local `TZ`/`tzset()` to Phase 5 rather than deferring.
- **Rationale**: the user confirmed a pump relay is coming, driven by day/night schedules.
  That makes wall-clock time a *control input*, not chart decoration — it must be local
  (DST moves the boundary twice a year) and explicitly flagged when unsynced (the WROOM-1
  has no battery-backed RTC, so `time()` returns 1970 after a power loss with the AP down).
- **Reference**: task file § Phase 5 Scope Change; commit `0edd908`.

### D5: PSRAM left disabled
- **Decision**: do not enable the board's 8 MB octal PSRAM in v1.
- **Rationale**: a 58 KB static ring and a small HTTP server do not need it, and leaving
  octal mode off avoids a class of configuration faults. GPIO 33–37 are avoided regardless
  since they route to PSRAM on R8 modules.

### D6: History not persisted across reboot
- **Decision**: no NVS, no flash writes. The day's chart is lost on power cycle.
- **Rationale**: simplicity and zero flash wear; accepted explicitly by the user at design
  time.

## Implementation

### Phases

| Phase | Outcome | Tests | Device (RAM / flash) |
|---|---|---|---|
| 1 — Foundation & test harness | Board config corrected (was declaring 8 MB flash / no PSRAM on a 16 MB N16R8), `platform` pinned to `espressif32@6.9.0`, custom `partitions.csv`, `[env:native]` stood up, `reading_store_core` TDD'd, secrets gitignored, `build/` (415 files) untracked | 21/21 | 4.0% / 6.6% |
| 2 — Sensor drivers | `bh1750`, `ds18b20_probe`, `level_switches`, `device_status`; Kconfig menu for pins/polarity/intervals | 21/21 | — |
| 2R — Remediation | **Human-caught at the phase gate**: none of Phase 2's four modules were linked into the image, and flash was declared 2 MB against a 3 MB partition. See *Lessons Learned* | — | — |
| 3 — Sampler & store | 30 s sampling, per-sensor offline escalation at 5 consecutive failures, TWDT scoped to the read window | 27/27 | 21.9% / 8.7% |
| 4 — Connectivity | Wi-Fi station + backoff reconnect + mDNS + serial-logged IP fallback; `wifi_backoff` pure module. Separate Espressif best-practice audit fixed D1–D4 deviations (`169b856`, `9024ed9`) | 31/31 | 29.1% / 27.5% |
| 5 — HTTP API + wall-clock | `/api/now`, `/api/history`, `reading_json` pure serializer, SNTP + local TZ, `epoch_sec` + `time_valid` | 39/39 | 32.6% / 29.9% |
| 6 — Web UI (final) | Embedded dashboard, `dashboard-logic.js` pure logic + 15 `node --test` tests, `embed_web_assets.py` link shim | 39 C + 15 JS = 54 | 32.6% / 30.4% |

### Key Components

| Module | Kind | Purpose |
|---|---|---|
| `lib/reading_store_core` | **pure** | Ring buffer arithmetic, wrap-and-overwrite, downsampling into a caller-supplied buffer. No FreeRTOS header, no lock. |
| `lib/reading_store` | device-only | Mutex ownership; delegates every operation to the core. No buffer arithmetic. |
| `lib/level_switches` | **pure** | N=3 debounce + 4-state truth table + configurable polarity. |
| `lib/wifi_backoff` | **pure** | Reconnect backoff schedule. |
| `lib/reading_json` | **pure** | JSON serialization; invalid readings emit `null`, never `0`. |
| `lib/bh1750`, `lib/ds18b20_probe` | device-only | I2C / 1-Wire drivers. |
| `lib/sensor_hub` | device-only | Per-sensor failure counting and offline escalation. |
| `lib/device_status` | device-only | `status_set()` seam; log-only in v1. |
| `src/sampler.c` | device-only | **Sole owner** of every sensor peripheral. |
| `src/web/dashboard-logic.js` | **pure** | Browser-side formatting/state logic; zero DOM, fetch, or timer access. |

### Technical Specifications
- `sensor_reading_t { uint32_t epoch_sec; float lux; float temp_c; level_state_t level; uint8_t valid; }`
  — ~20 bytes × 2,880 = ~58 KB, statically allocated (no heap fragmentation over long uptime)
- `level_state_t` has **five** values: `UNKNOWN`, `FULL`, `MID`, `LOW`, `FAULT`. `UNKNOWN` is
  a lifecycle state, not a switch combination — it is what the store returns before the
  first sample completes. A four-value enum would leave nothing legal to return at boot and
  invites a zero-valued `FULL` being reported as a measurement.
- `valid` is a bitfield: bits 0–2 used (lux, temp, time_valid), 5 spare.
- All pins, intervals, thresholds, debounce count, switch polarity, SNTP server, and the
  POSIX `TZ` string are Kconfig values under menu "Hydroponic Monitor" — no literals.

## Testing

### Strategy
Host-first. Every module that can run without ESP-IDF is compiled under `[env:native]`
against a lightweight type shim (`test/native/esp_shim.h`) with no FreeRTOS, and tested
there. Device-only modules (I2C/1-Wire drivers, the HTTP server, Wi-Fi) are verified by
clean cross-compile and are bench-verify-only by design. `test_build_src = no` guarantees
`src/` never enters the native binary.

### Results

| Test Type | Count | Pass Rate |
|---|---|---|
| Host unit (C, `pio test -e native`) | 39 | 100% |
| Host unit (JS, `node --test test/web/`) | 15 | 100% |
| Device integration | 0 | — (not run) |
| On-hardware / bench | 0 | — (not run) |

### Coverage
Host tests cover: ring push/wrap/count/downsample, `time_valid` through downsampling, the
level-switch truth table + debounce + flapping + polarity inversion (10 tests), sensor_hub
failure escalation, backoff scheduling, JSON serialization incl. null-for-invalid, and the
browser dashboard logic.

**Not covered by any automated test**: `esp_http_server` routing, browser rendering,
`app.js`, I2C/1-Wire transactions, Wi-Fi association, mDNS resolution, SNTP sync. These
are bench items, not gaps that host testing could have closed.

## Deployment

### Procedures
1. Copy `include/wifi_secrets.h.example` → `include/wifi_secrets.h` and fill in SSID/PSK.
   The build fails loudly without it (`#error` guarded by `__has_include`, AC-ERROR-6).
2. `pio run` — clean build expected, 0 warnings.
3. `pio run --target upload`
4. `pio device monitor` — **must be 115200**; see *Common Issues*.
5. Browse to `http://hydroponics.local/`, or to the IP logged over serial on connect.

### Configuration
`menuconfig` → "Hydroponic Monitor": sensor pins (level switches default GPIO 5 / 6),
per-switch polarity inverts, sample interval, debounce count, SNTP server, `TZ` string.
Flash size is pinned in `sdkconfig.defaults`, **not** `platformio.ini` — see *Common Issues*.

### Rollback
Single factory app partition, no OTA — rollback is reflashing a prior firmware image.
There is no persisted state to migrate: history is RAM-only by design, and no NVS is used.

## Maintenance

### Monitoring
Serial log at 115200 is the only operational channel in v1. `device_status.c`'s
`status_set()` is log-only; the LED consumer is deferred. `/api/now` exposes per-sensor
online/offline state and is the practical health check.

### Common Issues

| Issue | Resolution |
|---|---|
| Serial monitor shows byte garbage | Monitor baud mismatch. The IDF console is 115200; PlatformIO defaults to 9600. Fixed in `platformio.ini` (`monitor_speed = 115200`) — found on the first real bring-up attempt, after Phase 6. |
| "Flash memory size mismatch. Expected 16MB, found 2MB" | `board_upload.flash_size` configures only the **upload tool** — it never reaches `CONFIG_ESPTOOLPY_FLASHSIZE`. The build config needs a matching `sdkconfig.defaults` entry. |
| A new `lib/` module has no effect on device | It compiled and archived but was never linked, because nothing in `src/` references it. Check with `nm firmware.elf \| grep <symbol>` or compare flash delta against the prior build. |
| Water level reads `FULL` with nothing connected | Expected, and a hazard for the deferred relay work. GPIOs use internal pull-ups with no polarity invert, so an open circuit — including a broken wire — is indistinguishable from a healthy full tank. See *Future Considerations*. |
| `hydroponics.local` does not resolve | Client-side mDNS is unreliable (notably on Android). Use the IP logged over serial on every connect (AC-ERROR-7). |
| Web assets not in the firmware | Neither `EMBED_TXTFILES` nor `board_build.embed_txtfiles` links the generated object under `espressif32@6.9.0`. `embed_web_assets.py` reimplements the link step; verify with a clean rebuild (`rm -rf .pio/build && pio run`). |

### Operational Procedures
None recurring — the device is unattended and stateless across reboot. On firmware change,
rebuild clean when touching the embed path (the `extra_scripts` shim is DAG-sensitive).

## Lessons Learned

The full analysis is in the reflection; the four highest-value items:

1. **A green cross-compile is near-vacuous evidence in an embedded build.** Phase 2's four
   modules compiled, archived, and were silently dropped at link — `nm firmware.elf`
   matched zero Phase-2 symbols and flash was byte-identical to Phase 1. The phase's own
   "device build clean" gate could not distinguish this from success; a human re-running
   the build at the review checkpoint caught it (`7f85038`).
2. **Config that looks like it should reach the build often doesn't.** Same commit:
   `board_upload.flash_size = 16MB` never reaches `CONFIG_ESPTOOLPY_FLASHSIZE`, so every
   build printed a mismatch warning that went unread for an entire phase.
3. **Documented toolchain mechanisms can fail silently under a version pin.** Phase 6
   burned hours discovering that *both* official PlatformIO embed mechanisms fail under
   `espressif32@6.9.0` — one at build time, one only at final link.
4. **The pure/device split generalizes further than expected.** Invented under duress in
   Phase 1 to make a mutex-owning module testable, it applied unchanged to a state machine,
   backoff math, a serializer, and finally browser JavaScript.

Reference: `memory-bank/reflection/sensor-monitoring-dashboard-reflection.md`

## References
- Reflection: `memory-bank/reflection/sensor-monitoring-dashboard-reflection.md`
- Architecture / design: `memory-bank/creative/sensor-monitoring-dashboard-design.md`
- Task file (plan, spec, 14 ACs, execution state): `memory-bank/tasks/sensor-monitoring-dashboard.md`
- Roadmap feature: `memory-bank/roadmap/sensor-monitoring-dashboard.md`
- Patterns: `memory-bank/systemPatterns.md` § Pure-Logic/Device-Only Split, § Known Deviations
- Tooling notes: `memory-bank/techContext.md` § Web Asset Embedding
- Implementation timeline: `git log main..feature/sensor-monitoring-dashboard`

## Future Considerations

### Bench verification — the largest open item
No phase has been confirmed against real sensors. Outstanding, carried since Phase 1:
- One-shot boot read prints plausible lux and °C with sensors attached
- **Float-switch polarity** — a bench-determined value; the current default is unverified
- DS18B20 4.7 kΩ pull-up confirmed on the real probe
- `ping hydroponics.local` resolves from at least one client
- AC-ERROR-5 fault injection (pull a sensor mid-run → `offline` badge within 5 samples)
- AC-ERROR-7 IP fallback appears on serial on every connect
- Phase 6 bookmark load + 30 s auto-refresh in a real browser

Progress so far: first upload succeeded and the device boots. The console fix
(`monitor_speed = 115200`) was the prerequisite for everything above.

### Carried technical debt
- **D5** — Kconfig GPIO `range 1 48` admits nonexistent and fatal pins; enforced only by a
  prose comment. Escalates from cosmetic to safety-relevant the moment a GPIO drives a relay.
- **D6** — no coredump partition (`CONFIG_ESP_COREDUMP_ENABLE_TO_NONE=y`); an unattended
  crash leaves no post-mortem. Partially mitigated by the `esp32_exception_decoder` monitor
  filter, which only helps while a monitor is attached.
- `espressif/ds18b20` pinned to `^0.3.1` (0.4.0's manifest fails to parse under this
  toolchain's idf-component-manager) — revisit on the next toolchain bump.
- Two unused JS variables and a no-op `<style>` tag in the Phase 6 assets; no
  `Cache-Control` on the three static routes.

### For the deferred pump-relay feature
- **An open level-switch circuit currently reads `FULL`.** Internal pull-ups plus
  `CONFIG_HYDRO_LEVEL_INVERT_*` unset mean a disconnected float, a broken wire, and a
  genuinely full tank are indistinguishable. `FAULT` cannot fire for a broken wire. A pump
  interlock trusting `FULL` would run a dry reservoir — invert the polarity so an open
  circuit reads `LOW` (fail-safe), and treat this as a requirement, not a config default.
- Also deferred here by design: relay fail-safe state, the `LOW` dry-run interlock,
  `CONFIG_ESP_TASK_WDT_PANIC`, and the D5 pin guardrail.

### Not yet exercised
`/bmb:uat` never ran — the project had no UI at init, so `uat-config.md` and
`ux-patterns.md` were never created, and Phase 6 shipped the first web surface with no UAT
coverage on top of no hardware coverage.
