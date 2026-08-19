# Creative Design — sensor-monitoring-dashboard

**Task**: `sensor-monitoring-dashboard`
**Feature**: `sensor-monitoring-dashboard`
**Design type**: Architecture
**Produced by**: `/bmb:brainstorm` (conversational design)
**Date**: 2026-08-19
**Status**: Approved by user

---

## Context

The repository was a bare PlatformIO + ESP-IDF scaffold — `src/main.c` held an empty
`app_main()` and nothing else. The user's goal, in their own words: *"I can bookmark a page
on my browser that shows me how the hydroponic system is doing."* The existing setup is "a
bucket with a pump in it, no data collected."

Constraints fixed by the user before design began: LAN-only HTTP delivery (no MQTT, no
cloud, no companion app), no authentication, in-RAM FIFO history, no OTA, and monitoring
only in v1 with relay pump control deferred to v2.

---

## Decision 1 — Water level sensing: accept binary, or use both switches

The product brief called for "bucket fullness," but the confirmed hardware is a Piutouyar
**float switch** — a binary reed contact with no continuous output. There is no level
percentage to chart, and a 24-hour graph of a single boolean is close to worthless. This
mismatch was surfaced before any architecture work.

**Options considered**

| Option | Result | Cost |
|---|---|---|
| Accept binary | Badge showing full / not-full; history degrades to an event log | Zero |
| **Two switches at two heights** | Three ordered bands (`FULL` / `MID` / `LOW`) | One extra GPIO, ~10 lines |
| Add ultrasonic or eTape later | True continuous level | New hardware purchase |

**Decision: two switches.** The user already owned a 2-pack. It is the only option that
makes level worth charting, at negligible cost.

**Consequence the user identified and we kept — the fourth combination.** Of the four
possible switch states, one is physically impossible: the high switch floating while the
low switch is not. Water cannot be above the high mark and below the low mark
simultaneously. Rather than collapse it into a neighboring band, this is reported as an
explicit `FAULT` state indicating a stuck float, a broken wire, or a swapped connector.

This is free fault detection, it satisfies the fail-safe Guiding Principle, and it becomes
load-bearing in v2: a pump driven from a silently-stuck float is exactly how a reservoir
gets pumped onto the floor.

**Rejected sub-option**: reporting a fabricated percentage. Two switches give three ordered
bands, not a continuous measure. The UI reports the band honestly; if the user later
measures the two mount heights it can state a true range ("40–80%") rather than inventing
a single number.

---

## Decision 2 — Overall architecture

**Options considered**

**A — Minimal monolith.** No sampler task; the HTTP handler reads sensors inline and
returns server-rendered HTML with `<meta refresh>`. No history.
*Rejected.* Every browser refresh would hit the 1-Wire bus for a 750 ms conversion, full
page reloads fight the "visually attractive" requirement, it discards the FIFO-history
requirement outright, and it offers no seam for v2 relay control.

**B — Sampler task + JSON API + embedded page.** ← **chosen**
A sensor task samples on a fixed interval into a RAM ring buffer. `esp_http_server` serves
an embedded static page plus `/api/now` and `/api/history`; the page polls and charts.
Drivers sit behind `lib/` interfaces per the hardware-abstraction principle, which makes
the ring buffer, unit conversion, and debounce state machine host-testable.

**C — B plus push and a filesystem.** SSE/WebSocket push instead of polling, SNTP
wall-clock timestamps, and web assets on a LittleFS partition so the UI updates without
reflashing.
*Rejected for v1.* SSE holds a socket open per viewer, SNTP contradicts the LAN-only
requirement, and a separate asset partition means two artifacts to flash and keep in sync
— real cost for a single-user bookmark page. Every one of C's additions can be layered onto
B later without rework.

**Rationale for B**: it is the smallest design satisfying all three stated capabilities —
sensor reads, an attractive LAN page, and FIFO history — while leaving a clean seam for the
v2 relay (one more endpoint, one more `lib/` module).

---

## Decision 3 — Concurrency and shared state

`app_main()` performs setup and **returns**; FreeRTOS continues running. This satisfies the
"no blocking in `app_main`" Guiding Principle without a pointless idle loop.

Three concurrent actors: the sampler task, the `esp_http_server` task, and the IDF event
loop handling Wi-Fi/IP events.

Shared state is a single `reading_store` module owning the ring buffer and a mutex. The
sampler is the only writer; HTTP handlers are the only readers.

- **A mutex, not a FreeRTOS queue** — handlers need random access to the whole history, not
  a one-shot stream.
- **Sensor reads happen outside the lock.** A full sample cycle costs ~500 ms (DS18B20
  375 ms at 11-bit, BH1750 ~120 ms). The sampler reads everything first, then locks only
  long enough to copy 20 bytes.
- **Deliberate lock asymmetry.** Handlers acquire with a 100 ms timeout and return `503`
  rather than hanging; the sampler blocks indefinitely. The writer is trusted to be fast
  (microseconds); the reader is not trusted to wait.

**Decoupling sampling from connectivity is the point of the task split**: Wi-Fi loss does
not stop sampling, so the ring keeps filling while offline and the chart shows the outage
span on reconnect rather than a hole.

---

## Decision 4 — Test seam: link-time stubs vs runtime vtable

The project's only PlatformIO environment targets the board, so `pio test` requires
attached hardware — which blocks unattended TDD in `/bmb:build`. Fixing this was treated as
part of the design, not an afterthought.

**Options considered**

| Option | Verdict |
|---|---|
| Runtime driver vtable (struct of function pointers) | Rejected — buys hot-swappable drivers that will never be needed, at the cost of permanent indirection on-device |
| **Link-time substitution** | **Chosen** — `sensor_hub` depends on the three driver *headers*; the `[env:native]` target links stub implementations instead of the real ones |

Link-time substitution gives identical testability with zero runtime cost and no
indirection. It requires one small enabler: `test/native/esp_shim.h`, ~20 lines defining
`esp_err_t`, `ESP_OK`, `ESP_FAIL`, and no-op `ESP_LOG*` macros so logic modules compile off
the device.

Roughly 30 host-run tests become possible: ring buffer wrap and downsampling, the level
state machine across all four combinations plus debounce sequences, sampling failure paths,
and JSON serialization. Peripheral bus behavior remains bench-verified — that split is
stated explicitly rather than papered over.

---

## Decision 5 — History sizing and payload strategy

**Cadence: 30-second sampling, 24-hour window** — 2,880 samples, ~58 KB, statically
allocated so there is no heap fragmentation over long uptime.

Chosen on domain grounds: a hydroponic reservoir changes over hours and days, not seconds.
The questions the page must answer are "did the lights come on," "how warm did it get
today," and "how fast is the level dropping" — all of which need a wide window and none of
which need fast sampling. Finer alternatives (10 s / 6 h, 5 s / 1 h) were rejected for
losing the overnight picture.

**Two non-obvious constraints drove the API shape:**

1. **A full day of history cannot be serialized into RAM.** Fixed two ways — the history
   endpoint **downsamples** to a requested point count (default 180, ample for any chart
   width), and the response is **chunk-streamed** so peak RAM stays flat regardless of ring
   size.
2. **There is no clock on the board**, and SNTP would contradict the LAN-only requirement.
   Resolved by reporting only `uptime_sec` and having the **browser** convert: each response
   carries the device's current uptime, and the client subtracts the delta from its own
   wall clock. Accurate local timestamps with no RTC and no internet dependency.

History uses **parallel arrays** rather than an array of objects — roughly 60% smaller on
the wire, since field names are not repeated per sample.

**Failed reads are stored as invalid, never as `0`.** The per-sensor `valid` bitfield means
the chart draws a gap rather than a plunge to zero — a dead probe looks like a dead probe.

---

## Decision 6 — Failure handling

- **Independent sensor failure**: one retry inside the driver, then that sensor's `valid`
  bit is cleared for the cycle; the other sensors still record. Nothing aborts.
- **Escalation**: a per-sensor consecutive-failure counter promotes WARN to ERROR and marks
  the sensor **offline** after five straight failures, so the UI can say "temperature probe
  not responding" instead of showing an unexplained gap.
- **Watchdog**: the sampler subscribes to the IDF task watchdog, so a hung 1-Wire read
  reboots the device rather than leaving it alive but silently recording nothing. A
  self-healing reboot is the correct fail-safe for an unattended device.
- **Reconnection**: capped exponential backoff (1→2→4…→30 s) so a downed AP is not
  hammered; mDNS re-registers automatically.
- **Input**: query parameters are clamped, never fatal. The server does not return `500`
  for something a browser sent.

---

## Decision 7 — Secrets handling

The user asked for "a `.env` file with my Wi-Fi info, so long as you gitignore it."

**Chosen**: a gitignored `include/wifi_secrets.h` with `#define WIFI_SSID` / `WIFI_PASS`,
alongside a committed `include/wifi_secrets.h.example`.

C has no dotenv reader, so a literal `.env` would require a PlatformIO Python build script
to parse it into `-D` flags — added machinery, plus quoting hazards for passwords
containing `$`, `!`, or spaces. The header is the same bargain in a format the toolchain
consumes natively, and a missing file fails the build loudly instead of silently shipping
empty credentials.

Stated plainly to the user and accepted: credentials live in plaintext inside the flash
image. Only runtime NVS provisioning avoids that, which is overkill for a personal LAN
device.

---

## Decision 8 — Deferred, with seams left

- **Status LED** — deferred. Most ESP32-S3 devkits carry an addressable RGB LED that would
  let the user diagnose a Wi-Fi failure without a laptop (the only UI is a web page they
  cannot reach when Wi-Fi is down). The user chose to defer the LED but keep the reporting
  seam, so a `device_status` module exposing `status_set(state)` is built now as log-only.
  A future LED consumer is a drop-in with no rework.
- **Relay pump control** — v2. Consumes the `FAULT` level state as a hard interlock.
- **Two-tier history** (fine recent + coarse older) — deferred; a real complexity jump,
  worth adding only if the 24-hour window proves insufficient.
- **SSE/WebSocket push, SNTP, LittleFS assets** — approach C above; layerable later.

---

## Board configuration findings

Surfaced during design and folded into Phase 1:

- `platformio.ini` declares `board = esp32-s3-devkitm-1` (8 MB flash, no PSRAM). The actual
  hardware is a Hosyond ESP32-S3-WROOM-1 **N16R8** — 16 MB flash, 8 MB octal PSRAM. Needs a
  corrected board target and a custom partition table.
- **PSRAM stays disabled in v1** — a 58 KB ring and a small HTTP server do not need 8 MB,
  and leaving octal mode off avoids a class of configuration faults.
- **GPIO 33–37 are avoided regardless**, being routed to the PSRAM on R8 modules. Also
  avoided: 26–32 (SPI flash), 0/3/45/46 (strapping), 19/20 (USB), 43/44 (UART0).
- **`platform = espressif32` is unpinned.** Beyond reproducibility, the pin determines
  whether ESP-IDF exposes the `i2c_master` API (5.2+) or the deprecated legacy `i2c`
  driver — and the BH1750 driver must be written against whichever applies. Pinning is a
  Phase 1 deliverable, not a nicety.

---

## Open items requiring bench verification

These were flagged as genuinely undetermined rather than assumed:

- **Float-switch polarity.** The Piutouyar switches are reversible; mounting orientation
  swaps normally-open and normally-closed. Hence per-switch polarity configuration rather
  than a hardcoded assumption.
- **DS18B20 pull-up.** The DROK adapter board may already populate the 4.7 kΩ resistor.
  Adding the loose one as well puts them in parallel (~2.35 kΩ) — usually still functional,
  but worth confirming before assuming it is needed.
- **Status LED pin**, if the LED is added later — commonly GPIO 48 on ESP32-S3 devkits, but
  unconfirmed for this Hosyond board.
