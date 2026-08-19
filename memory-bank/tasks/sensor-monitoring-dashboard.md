---
slug: sensor-monitoring-dashboard
legacy_id:
feature: sensor-monitoring-dashboard
status: PLANNING_COMPLETE
---

# sensor-monitoring-dashboard: Hydroponic Sensor Monitoring Dashboard

**Complexity**: Level 4
**Status**: PLANNING_COMPLETE
**Roadmap**: sensor-monitoring-dashboard
**Branch**: feature/sensor-monitoring-dashboard
**Worktree**: N/A (primary checkout)

## Task Description

Build the firmware for an ESP32-S3-N16R8 that monitors a hydroponic reservoir and serves
the readings as a LAN-hosted web page.

The repository today is a bare PlatformIO + ESP-IDF scaffold — `src/main.c` contains only
an empty `app_main()`. This task delivers essentially the entire firmware.

**Hardware in hand** (confirmed by the user, 2026-08-19):

| Sensor | Part | Interface |
|---|---|---|
| Ambient light | HiLetgo GY-302 / BH1750 module | I2C, address `0x23` (ADDR tied low) |
| Water temperature | DROK waterproof DS18B20 + 4.7 kΩ pull-up | 1-Wire |
| Water level | 2 × Piutouyar PP plastic float switch | Digital GPIO, internal pull-up |
| Board | Hosyond ESP32-S3-WROOM-1, N16R8 | 16 MB flash, 8 MB octal PSRAM |

**Level sensing is three-band, not continuous.** A float switch is a binary reed contact,
so two of them mounted at a high and a low water mark yield three ordered states plus one
diagnostic state:

| High switch | Low switch | State |
|---|---|---|
| Floating | Floating | `FULL` — at or above the high mark |
| Not floating | Floating | `MID` — between the marks |
| Not floating | Not floating | `LOW` — below the low mark |
| Floating | Not floating | `FAULT` — physically impossible; stuck float, broken wire, or swapped connector |

The `FAULT` state is reported, never guessed at. In the deferred v2 (relay pump control)
it becomes a hard interlock, because a pump driven from a stuck float is how a reservoir
gets emptied onto the floor.

**Scope boundaries.** Version 1 is monitoring only — the device actuates nothing. HTTP over
the LAN is the only delivery channel: no MQTT, no cloud, no companion app, no
authentication, and no OTA (the user flashes manually). History lives in RAM and is
deliberately lost on reboot. Relay control is out of scope; this task only leaves the seams
for it.

**Board configuration is wrong today and must be corrected in Phase 1.** `platformio.ini`
declares `board = esp32-s3-devkitm-1`, which specifies 8 MB flash and no PSRAM — the actual
hardware is 16 MB flash with 8 MB octal PSRAM. The platform version is also unpinned, which
is not merely a reproducibility concern here: it decides whether ESP-IDF exposes the
`i2c_master` API (5.2+) or the deprecated legacy `i2c` driver, and the BH1750 driver must be
written against whichever one the pin resolves to.

**PSRAM stays disabled in v1.** A 58 KB ring buffer and a small HTTP server do not need
8 MB, and leaving octal PSRAM off avoids a class of configuration faults. GPIO 33–37 are
avoided regardless, since they are routed to the PSRAM on R8 modules.

## Specification

**Feature Type**: End-User Feature
**Primary Persona**: The repo owner — a single hobbyist running one personal hydroponic
reservoir ("a bucket with a pump in it, no data collected"). Goal, in their words: *"I can
bookmark a page on my browser that shows me how the hydroponic system is doing."* There is
no second persona, no operator role, and no authentication.
**Creative Exploration Needed**: No — the architecture, data model, API shape, and failure
strategy were settled with the user during `/bmb:brainstorm` and are recorded in
`memory-bank/creative/sensor-monitoring-dashboard-design.md`. Four bench-verification
unknowns remain and are listed under *Creative Exploration Needed* below; none of them
change the design, they only fix a configuration value or an API selection.

### Invocation Method
- **Location**: A web browser on any device joined to the same LAN as the ESP32-S3. There
  is no app, no installer, and no companion device — the browser is the entire client.
- **Element**: The bookmarked URL `http://hydroponics.local/`, served by the
  `esp_http_server` instance created in `src/http_api.c` (Phase 5) with the page body
  embedded from `src/web/index.html`, `src/web/style.css`, and `src/web/app.js` via
  `EMBED_FILES` in `src/CMakeLists.txt` (Phase 6).
- **Visibility**: Always reachable while the device is powered and associated with the AP.
  The hostname is registered over mDNS by `src/wifi_conn.c` (Phase 4) and re-registered on
  every reconnect, so a DHCP lease change does not break the bookmark. This is not a
  convenience: a changing IP would break the one thing the user defined as success.
- **Navigation**: One hop. Open bookmark → dashboard. No login, no landing page, no menu,
  no route beyond `/`. The two JSON endpoints (`/api/now`, `/api/history`) are consumed by
  `app.js`, not navigated to by the user, though both are directly `curl`-able for
  verification.
- **Confidence**: HIGH on the invocation shape (single URL, mDNS, no auth) — explicitly
  confirmed by the user. HIGH on `esp_http_server` availability (already present in the IDF
  component set per `techContext.md` § API & Communication). No prior UI, design system, or
  routing exists in this repository to pattern-match against — `src/main.c` is an empty
  `app_main()` — so the page is authored from scratch in Phase 6 with no external assets
  (no CDN, no framework), because the LAN has no guaranteed internet path.

### Success Criteria
- **User sees**: three current values — water temperature in °C, ambient light in lux, and
  the water-level band rendered as one of `FULL` / `MID` / `LOW` / `FAULT` — above a chart
  covering the past 24 hours. Each metric carries a state badge: live, `offline` (sensor
  not responding), or `FAULT` (level only). Before the first sample completes, the page
  reads *"waiting for first reading"* rather than rendering zeros.
- **Verifiable at**: `http://hydroponics.local/` in the browser; independently at
  `http://hydroponics.local/api/now` and `http://hydroponics.local/api/history?points=180`
  via `curl`, which return the same underlying readings as JSON.
- **Data persisted**: an in-RAM ring buffer owned by `lib/reading_store/src/reading_store.c`
  — 2,880 entries of `sensor_reading_t { uint32_t uptime_sec; float lux; float temp_c;
  level_state_t level; uint8_t valid; }` (~20 bytes each, ~58 KB total), statically
  allocated so there is no heap fragmentation over long uptime. `valid` is a per-sensor
  bitfield: a failed read is stored with its bit cleared, **never** as `0.0`, so the chart
  draws a gap instead of a line to zero. History is deliberately **not** persisted across
  reboot — no NVS, no flash writes — which the user accepted during design. PSRAM stays
  disabled in v1; 58 KB does not need 8 MB.
- **Observable within**: the first reading appears within ~30 seconds of the device booting
  and joining Wi-Fi (one sample interval). Thereafter values update every 30 seconds without
  the user reloading the page. Wall-clock chart labels are computed **in the browser**: the
  device has no RTC and no SNTP (LAN-only, by requirement), so every response carries the
  device's current `uptime_sec` and `app.js` subtracts the delta from the browser's own
  clock.

### Acceptance Criteria

#### AC-ENTRY-1: The bookmarked URL loads the dashboard with no authentication
**Priority**: MUST
**Given** the device is powered and associated with the LAN AP
**When** the user opens `http://hydroponics.local/` in a browser on the same LAN
**Then** the dashboard page is served with HTTP 200 and rendered; no login form, credential
prompt, token, or redirect is presented at any point

#### AC-ENTRY-2: The bookmark keeps working across device reboots and DHCP lease changes
**Priority**: MUST
**Given** the user has bookmarked `http://hydroponics.local/`
**When** the device reboots, or the AP reassigns it a different IP address
**Then** `hydroponics.local` resolves to the new address without the user editing the
bookmark; mDNS is re-registered on reconnect by `src/wifi_conn.c`

#### AC-HAPPY-1: The dashboard shows all three live values and a 24-hour chart, refreshing on its own
**Priority**: MUST
**Given** the device has been sampling for at least one interval
**When** the user views `http://hydroponics.local/`
**Then** the page displays the current water temperature (°C), light level (lux), and water
level band, plus a chart of all three over the past 24 hours; and the displayed values
advance on their own within ~30 seconds without the user reloading the page

#### AC-HAPPY-2: `/api/now` returns the latest reading with the device uptime for clock reconstruction
**Priority**: MUST
**Given** at least one sample has been recorded
**When** `GET /api/now` is requested
**Then** the response is well-formed JSON carrying `lux`, `temp_c`, `level`, per-sensor
validity, and the device's current `uptime_sec`; any sensor whose `valid` bit is clear
serializes as `null`, never as `0`

#### AC-HAPPY-3: `/api/history` returns downsampled parallel arrays without a RAM spike
**Priority**: MUST
**Given** the ring buffer holds up to 2,880 samples
**When** `GET /api/history?points=180` is requested
**Then** the response is parallel arrays — `{"t":[…],"lux":[…],"temp_c":[…],"level":[…]}`,
not an array of objects — downsampled evenly to the requested point count, and emitted by
chunked streaming so peak RAM is flat regardless of how full the ring is; the arrays are of
equal length and index-aligned

#### AC-ERROR-1: A failed sensor is reported as offline, not as a plausible-looking zero
**Priority**: MUST
**Given** one sensor fails to read for five consecutive sampling cycles
**When** the user views the dashboard
**Then** that metric is labeled **offline** (e.g. "temperature probe not responding") and
the chart draws a **gap** across the affected span; the value is never shown as `0`, and a
stale prior value is never presented as current. The other two sensors continue to record
and display normally.

#### AC-ERROR-2: The impossible float-switch combination is surfaced as `FAULT`, never resolved to a band
**Priority**: MUST
**Given** the high float reads floating while the low float does not — physically impossible,
so a stuck float, broken wire, or swapped connector
**When** that combination survives the N-consecutive-agreeing-sample debounce (N=3) in
`lib/level_switches/src/level_switches.c`
**Then** the level is reported as `FAULT` and the dashboard shows a `FAULT` warning; the
firmware does not select `FULL`, `MID`, or `LOW` as a best guess, and does not average or
interpolate toward a neighboring band

#### AC-ERROR-3: Wi-Fi loss does not interrupt sampling, and the outage span is visible on reconnect
**Priority**: MUST
**Given** the device is sampling normally
**When** the AP goes away and later returns
**Then** the sampler task keeps filling the ring buffer throughout the outage; the device
reconnects on capped exponential backoff (1→2→4→…→30 s) and re-registers mDNS; and when the
user reloads the page the chart shows the readings recorded **during** the outage rather
than a gap. Only the delivery channel was down, not the measurement.

#### AC-ERROR-4: Bad query input is clamped, never fatal
**Priority**: MUST
**Given** the HTTP API is serving
**When** `GET /api/history` is called with `?points=` absent, non-numeric, zero, negative, or
larger than the ring capacity
**Then** the value is clamped into range (default 180) and a valid response is returned;
the request never crashes the server task, never returns a 5xx, and never reads outside the
ring buffer

#### AC-ERROR-5: A contended store lock returns 503 rather than hanging the HTTP task
**Priority**: MUST
**Given** the sampler task holds the `reading_store` mutex
**When** an HTTP handler attempts to acquire it and does not get it within 100 ms
**Then** the handler returns HTTP 503 and releases the request; it does not block
indefinitely, and the HTTP server remains able to serve the next request. (The sampler,
conversely, blocks on the lock — its critical section is a 20-byte copy, and the ~500 ms of
sensor reads happen outside the lock.)

#### AC-ERROR-6: A missing Wi-Fi secrets file fails the build loudly
**Priority**: MUST
**Given** a fresh clone where gitignored `include/wifi_secrets.h` has not been created from
`include/wifi_secrets.h.example`
**When** `pio run` is executed
**Then** the build fails with a message naming the missing file and the example to copy; it
does not silently substitute empty credentials, and it does not produce a binary that boots
and fails to associate for unexplained reasons

#### AC-ASYNC-1: The pre-first-sample state is explicit, not an empty or zeroed dashboard
**Priority**: MUST
**Given** the device has booted and joined Wi-Fi but has not yet completed its first sample
**When** the user opens the dashboard or calls `GET /api/now`
**Then** the level reports `UNKNOWN` with every sensor marked invalid, and the page displays
"waiting for first reading"; no metric renders as `0`, blank, or `--` in a way that could be
mistaken for a measurement

#### AC-ASYNC-2: A hung sensor read reboots the device rather than silently freezing the data
**Priority**: MUST
**Given** the sampler task is subscribed to the FreeRTOS task watchdog
**When** a sensor read (e.g. a 1-Wire transaction) blocks past the watchdog period
**Then** the watchdog fires and the device reboots; the failure mode is a visible restart
with a fresh (empty) history, not a page that keeps serving an unchanging reading forever

### Scope Boundaries
- **In scope**: reading three sensors (BH1750 lux over I2C at `0x23`; DS18B20 water
  temperature over 1-Wire at 11-bit / 375 ms conversion; two float switches on GPIO with
  internal pull-ups) on a 30-second cadence; a three-band level state machine plus `FAULT`
  with N=3 consecutive-agreeing-sample debounce; a 24-hour in-RAM ring buffer; Wi-Fi station
  mode with backoff reconnect and an mDNS hostname; an `esp_http_server` serving `/`,
  `/api/now`, and `/api/history` with downsampling and chunked streaming; and a
  firmware-embedded HTML/CSS/JS dashboard. Also in scope as enabling work: correcting
  `platformio.ini` to the N16R8 board, pinning the `espressif32` platform version, adding
  `partitions.csv` for 16 MB flash, and adding the `[env:native]` host test environment with
  `test/native/esp_shim.h` and link-time driver stubs.
- **Out of scope**: relay or pump control of any kind (v1 actuates nothing — deferred to
  v2, which will consume `FAULT` as a hard interlock); MQTT; any cloud service or remote
  access from outside the LAN; a companion mobile app; authentication, users, or roles; OTA
  updates (the user flashes over USB); persisting history across reboot; SNTP or an RTC;
  pH, EC/TDS, humidity, or any sensor not in the confirmed hardware table; alerting,
  notifications, or email; multi-device or multi-reservoir support; PSRAM usage; and an
  onboard-LED status indicator (the `device_status` module is built with `status_set()` but
  is log-only in v1).
- **Dependencies**: a 2.4 GHz Wi-Fi AP with WPA2 and a client OS with working mDNS
  resolution; ESP Component Registry packages `espressif/onewire_bus` and `espressif/ds18b20`
  declared in `src/idf_component.yml`; the pinned `espressif32` platform version, which
  determines the ESP-IDF release and therefore whether the BH1750 driver is written against
  the `i2c_master` API (IDF 5.2+) or the legacy `i2c` driver; and a gitignored
  `include/wifi_secrets.h` created from the committed `.example`.
- **NFR implications**:
  - *Memory* — the ring buffer is statically allocated (no `malloc`) specifically so a
    device intended to run for weeks does not fragment its heap; the history response is
    chunk-streamed because the full ring cannot be serialized into RAM.
  - *Availability* — sampling is independent of connectivity by design; the sampler is
    watchdog-subscribed; HTTP handlers time-box their lock acquisition.
  - *Security* — none, deliberately. No authentication, and Wi-Fi credentials live in
    plaintext in flash. The user was told and explicitly accepted this for a personal LAN
    device. Anyone on the LAN can read the dashboard; there is nothing to write.
  - *Accessibility* — level state must be conveyed by text (`FULL` / `MID` / `LOW` /
    `FAULT`), not colour alone; `FAULT` and `offline` are words on the page, not just a red
    badge.
  - *Fail-safe (per `systemPatterns.md`)* — every failure path has a defined safe state:
    invalid reading rather than a fabricated zero, `FAULT` rather than a guessed band,
    `UNKNOWN` before the first sample, 503 rather than a hung handler, reboot rather than a
    silently dead sampler.
  - *Guiding principles* — all sensor access goes through `lib/` driver interfaces so
    `sensor_hub` and the state machine are host-testable; `app_main()` wires and returns;
    every `esp_err_t` is checked; pins, intervals, thresholds, and debounce count are Kconfig
    values, not literals; each module carries its own `static const char *TAG`.

### Creative Exploration Needed
Specification is concrete — proceed to implementation planning. No design questions remain
open. Four **bench-verification** unknowns are flagged LOW confidence; each resolves to a
configuration value or an API selection at build time, and none of them alters the
architecture:

1. **Float-switch polarity (LOW)** — the Piutouyar switches are reversible and the mapping
   from "floating" to logic level depends on physical mounting orientation. Polarity must be
   a Kconfig option, determined at the bench in Phase 2 by physically raising and lowering
   each float. Do not hard-code an assumed active level.
2. **DS18B20 pull-up resistor (LOW)** — whether the DROK adapter board already populates the
   4.7 kΩ 1-Wire pull-up is unverified. Confirm at the bench in Phase 2 before adding a loose
   resistor; a doubled pull-up and a missing one fail differently and both look like a bad
   probe.
3. **ESP-IDF version behind the platform pin (LOW)** — the `espressif32` version chosen in
   Phase 1 decides whether `i2c_master` (IDF 5.2+) or the deprecated legacy `i2c` driver is
   available. The BH1750 driver in Phase 2 must be written against whichever the pin actually
   resolves to; resolve and record the version in Phase 1 before writing driver code.
4. **Onboard status LED (LOW)** — whether the Hosyond ESP32-S3-WROOM-1 board exposes an
   addressable status LED, and on which GPIO, is unconfirmed. This is why `device_status` is
   log-only in v1: the `status_set()` seam is built now and the LED consumer is deferred
   until the pin is confirmed on the bench.

## User Journey Definition

**Feature Type**: End-User Feature
**Creative Phase Required**: No — architecture design completed during `/bmb:brainstorm`
(see `memory-bank/creative/sensor-monitoring-dashboard-design.md`)

### Invocation Method (End-User Features)
- **Location**: Any web browser on the same LAN as the device
- **Element**: The URL `http://hydroponics.local/` — bookmarked by the user
- **Visibility**: Always available while the device is powered and joined to Wi-Fi
- **Navigation**: Single URL, no authentication, no login, no menu — the bookmark opens the
  dashboard directly

### Success Criteria (End-User Features)
- **User sees**: current water temperature in °C, current light level in lux, current level
  band (`FULL` / `MID` / `LOW` / `FAULT`), and a chart of all three over the past 24 hours
- **User can verify at**: `http://hydroponics.local/`
- **Data persisted**: 2,880 samples (30-second interval × 24 hours) in an in-RAM ring
  buffer. Deliberately **not** persisted across reboot — accepted by the user during design.
- **Observable within**: 30 seconds of the device booting and joining Wi-Fi (the first
  sample interval)

### NFR Verification (Infrastructure Features)
Not applicable — this is an End-User Feature. Per-phase hardware verification steps are
recorded under Per-Phase Test Guidance below.

### Acceptance Criteria
- AC-ENTRY-1: With the device powered and on the LAN, a user navigates to
  `http://hydroponics.local/` in a browser and the dashboard page loads without
  authentication.
- AC-HAPPY-1: The loaded dashboard displays the current water temperature (°C), light level
  (lux), and water-level band, and renders a 24-hour chart of all three; values refresh
  without a manual page reload.
- AC-ERROR-1: When a sensor fails to read for five consecutive cycles, the dashboard marks
  that metric **offline** and the chart draws a gap for the affected span — it never
  displays `0` or a stale value as though it were a live reading.
- AC-ERROR-2: When the two float switches report the physically impossible combination, the
  dashboard displays a `FAULT` warning for water level rather than selecting one of the
  three normal bands.
- AC-ERROR-3: When Wi-Fi drops, sampling continues uninterrupted; on reconnect the chart
  shows the samples recorded during the outage rather than a gap.

## Test Strategy

### Approach
- **Emphasis**: unit-heavy on pure logic, with explicit manual hardware verification per
  phase. This is a deliberate split: the peripheral drivers cannot be meaningfully unit
  tested without the physical bus, but the ring buffer, level state machine, sampling
  failure handling, and JSON serialization are all pure logic and carry most of the
  defect risk.
- **Target test count**: ~30 host-run tests. Justification for exceeding 20: five
  independent logic modules are introduced, and the level state machine alone has four
  switch combinations crossed with a multi-sample debounce sequence.

**Prerequisite — this is the enabling work.** The project currently has exactly one
PlatformIO environment, targeting `esp32-s3-devkitm-1`, so `pio test` requires a board to be
physically attached. That blocks unattended TDD in `/bmb:build`. Phase 1 therefore adds an
`[env:native]` host environment plus a small `test/native/esp_shim.h` (defining `esp_err_t`,
`ESP_OK`, `ESP_FAIL`, and no-op `ESP_LOG*` macros) so logic modules compile off-device.
Every test below runs on the host.

**Driver substitution is link-time, not runtime.** `sensor_hub` depends on the three driver
*headers*; the native environment links stub implementations of those headers instead of
the real ones. No function-pointer vtable and no runtime indirection — the device build
pays nothing for testability.

### File Organization
- **New test files**:
  - `test/test_reading_store/test_reading_store.c` — ring buffer behavior and downsampling
  - `test/test_level_switches/test_level_switches.c` — level state machine and debounce
  - `test/test_sensor_hub/test_sensor_hub.c` — sampling failure paths against stub drivers
  - `test/test_reading_json/test_reading_json.c` — serialization shape and invalid-value handling
  - `test/native/esp_shim.h` — host-side ESP-IDF type/macro shim (support, not a test)
  - `test/native/stubs/` — stub implementations of the three driver headers (support)
- **Extend existing**: none. `test/` currently holds only the PlatformIO-generated README.

### What NOT to Test
- BH1750 I2C register transactions — requires the physical bus; verified at the bench.
- DS18B20 1-Wire timing and CRC — depends on RMT peripheral timing; verified at the bench.
- Actual float-switch polarity — depends on physical mounting orientation; determined and
  configured at the bench.
- Wi-Fi association, reconnection backoff, and mDNS registration — require a real AP.
- `esp_http_server` request routing — framework behavior; covered by manual `curl` checks.
- The embedded HTML/CSS/JS — no browser test harness in scope for v1.

### Per-Phase Test Guidance
- **Phase 1** — ~10 tests. `reading_store`: push into an empty ring; fill exactly to
  capacity; wrap and overwrite the oldest; report correct count while partially filled;
  downsample 2,880 samples to 180 evenly; downsample when fewer samples exist than
  requested; request more points than the ring holds; read from an empty ring.
  *Hardware verification*: both environments build; the board boots and prints over serial.
- **Phase 2** — ~8 tests. `level_switches`: each of the four switch combinations maps to the
  correct state; a state change is rejected until N consecutive agreeing samples; a
  flapping sequence never commits; a sustained change does commit; `FAULT` is reported
  rather than resolved to a neighboring band.
  *Hardware verification*: serial log prints plausible lux, °C, and level every 30 s;
  float-switch polarity confirmed by physically raising and lowering each float; confirm
  whether the DROK adapter board already populates the 4.7 kΩ pull-up before adding the
  loose resistor.
- **Phase 3** — ~6 tests. `sensor_hub` against stub drivers: one sensor fails and the other
  two still record; all three fail; the consecutive-failure counter increments and resets
  correctly; a sensor is marked offline at the fifth consecutive failure; a successful read
  clears the offline state.
  *Hardware verification*: ring fills correctly across several hours, confirmed by serial dump.
- **Phase 4** — no host tests (all behavior requires a real AP).
  *Hardware verification*: `ping hydroponics.local` resolves; the device rejoins
  automatically after the AP is rebooted; sampling continues throughout the outage.
- **Phase 5** — ~6 tests. `reading_json`: parallel-array output shape; an invalid reading
  serializes as `null` rather than `0`; `?points=` clamps above the ring size; non-numeric
  `?points=` falls back to the default; empty-ring response is well-formed; chunk
  boundaries do not corrupt the emitted JSON.
  *Hardware verification*: `curl http://hydroponics.local/api/now` and `/api/history`
  return valid JSON.
- **Phase 6** — no host tests.
  *Hardware verification*: the page loads from a bookmark, values refresh without a manual
  reload, the 24-hour chart renders, and offline/`FAULT` badges appear when a sensor is
  physically disconnected.

## Implementation Roadmap

### New Source Files (pin path + extension)

Per `systemPatterns.md` § Code Organization Patterns: C only (`.c` / `.h`); one `lib/`
subdirectory per driver or cohesive subsystem, each with `include/` and `src/`; `src/` holds
`app_main()` and task wiring and stays thin.

Phase 1 — foundation and test harness:
- [ ] `platformio.ini` — **extend**: pin `platform = espressif32@<version>`; retarget the
      board to the N16R8 (16 MB flash, PSRAM disabled); add `[env:native]`
- [ ] `partitions.csv` — custom partition table for 16 MB flash
- [ ] `test/native/esp_shim.h` — host-side `esp_err_t` / `ESP_OK` / `ESP_LOG*` shim
- [ ] `lib/reading_store/include/reading_store.h` — ring buffer interface
- [ ] `lib/reading_store/src/reading_store.c` — ring buffer + mutex + downsampling
- [ ] `test/test_reading_store/test_reading_store.c`
- [ ] `include/wifi_secrets.h.example` — committed template
- [ ] `.gitignore` — **extend**: ignore `include/wifi_secrets.h`; untrack `build/`

Phase 2 — sensor drivers:
- [ ] `src/idf_component.yml` — declares `espressif/onewire_bus`, `espressif/ds18b20`
- [ ] `lib/bh1750/include/bh1750.h`, `lib/bh1750/src/bh1750.c` — I2C lux driver
- [ ] `lib/ds18b20_probe/include/ds18b20_probe.h`, `lib/ds18b20_probe/src/ds18b20_probe.c`
- [ ] `lib/level_switches/include/level_switches.h`, `lib/level_switches/src/level_switches.c`
- [ ] `lib/device_status/include/device_status.h`, `lib/device_status/src/device_status.c`
      — `status_set()` seam; log-only in v1, LED consumer deferred
- [ ] `test/test_level_switches/test_level_switches.c`

Phase 3 — sampler and store integration:
- [ ] `lib/sensor_hub/include/sensor_hub.h`, `lib/sensor_hub/src/sensor_hub.c`
- [ ] `test/native/stubs/bh1750_stub.c`, `ds18b20_probe_stub.c`, `level_switches_stub.c`
- [ ] `test/test_sensor_hub/test_sensor_hub.c`
- [ ] `include/sampler.h`, `src/sampler.c` — sampler task, mutex discipline, watchdog

Phase 4 — connectivity:
- [ ] `include/wifi_conn.h`, `src/wifi_conn.c` — station mode, backoff reconnect, mDNS

Phase 5 — HTTP API:
- [ ] `lib/reading_json/include/reading_json.h`, `lib/reading_json/src/reading_json.c`
- [ ] `test/test_reading_json/test_reading_json.c`
- [ ] `include/http_api.h`, `src/http_api.c` — `esp_http_server`, chunked streaming

Phase 6 — web UI:
- [ ] `src/web/index.html`, `src/web/style.css`, `src/web/app.js`
- [ ] `src/CMakeLists.txt` — **extend**: `EMBED_FILES` for the web assets. Note this file is
      currently PlatformIO-generated boilerplate; from this phase it becomes hand-maintained.
- [ ] `src/main.c` — **extend**: `app_main()` wiring for all subsystems

### Phases
- [ ] Phase 1: Foundation & test harness — board config corrected, `[env:native]` running,
      ring buffer TDD'd, secrets file gitignored
- [ ] Phase 2: Sensor drivers — all three sensors read on hardware, printed over serial
- [ ] Phase 3: Sampler & store — 30-second sampling into the ring, failure escalation,
      watchdog coverage
- [ ] Phase 4: Connectivity — Wi-Fi station with backoff reconnect and mDNS hostname
- [ ] Phase 5: HTTP API — `/api/now` and `/api/history` with downsampling and chunked
      streaming
- [ ] Phase 6: Web UI — embedded page, live values, 24-hour chart, offline/`FAULT` badges

## Creative Phases

- [x] Architecture design → complete (`memory-bank/creative/sensor-monitoring-dashboard-design.md`)

---

## Execution State

**Build Status**: IDLE
**Current Phase**: BUILD
**Last Completed**: N/A
**Can Resume**: NO

**BRAINSTORM CRITIQUE**: skipped — unresolved:no-companion (glob=∅)

The `creative-critique` seam is configured to `codex` in `memory-bank/projectConfig.md`, but the
Codex companion is not installed on this machine (`~/.claude/plugins/**/codex-companion.mjs`
returned no matches). Per `context/agent-backends.md`, the adversarial critique pass **skips**
rather than falling back when Codex is unreachable, and `availability: auto` suppresses the
warning banner. The brainstorm artifacts therefore carry **no independent adversarial review**.
Installing the `codex@openai-codex` plugin self-enables this pass for future runs.

**Gates run**: taxonomy lint CLEAN (13 ACs, 0 errors, 0 warnings) · concrete-spec validation
PASS (End-User Feature) · glossary skipped (not built) · Test Strategy populated.

### Active Sub-Agents
(none)

### Completed Steps
(none)
