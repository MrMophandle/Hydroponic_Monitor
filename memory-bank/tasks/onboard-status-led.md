---
slug: onboard-status-led
legacy_id:
feature: onboard-status-led
status: COMPLETE
---

# onboard-status-led: Onboard Status LED

**Complexity**: Level 3
**Status**: COMPLETE
**Bench Verification**: PARTIAL (2026-08-21) — 4 of 6 items confirmed on hardware; see § Bench Verification in the archive
**Archived**: memory-bank/archive/onboard-status-led-archive.md
**Completed**: 2026-08-21
**Reflection**: memory-bank/reflection/onboard-status-led-reflection.md
**Roadmap**: onboard-status-led
**Branch**: feature/onboard-status-led
**Worktree**: N/A

## Task Description

Drive the ESP32-S3-N16R8's onboard addressable RGB LED (WS2812, GPIO 48) as a
firmware **reachability** indicator, so the device reports whether its dashboard
is reachable without a browser or a serial console attached.

Three presentation states, derived from exactly two facts:

| LED | Meaning |
|---|---|
| green, solid | Wi-Fi associated **and** HTTP server running — dashboard reachable |
| red, blinking | booting, connecting, or reconnecting — not reachable, still trying |
| red, solid | HTTP server never started — permanent, needs a reflash |

### Why this is more than an LED driver

`lib/device_status/` was built in Phase 2 of `sensor-monitoring-dashboard` for
exactly this consumer, and its header records the blocker: *"the Hosyond
ESP32-S3-WROOM-1's onboard status LED availability/GPIO is unconfirmed … the
hardware consumer is deferred until that pin is confirmed on the bench."* That
pin is now identified (GPIO 48, per vendor Q&A; the vendor's own documentation
disagrees and cites GPIO 47 — hence a Kconfig `choice` plus a bench-confirmation
step rather than a hard-coded constant).

But the runtime status model the LED needs **does not exist today**:

1. `status_set()` is called from **five sites, all in `src/main.c` (107, 109, 111,
   131, 169), all during boot.** Nothing calls it at runtime; after boot the
   status never changes again.
2. It is a fire-and-forget setter with **no stored state and no getter** — last
   caller wins. "Wi-Fi up AND HTTP up" is a conjunction of two independent facts,
   which a single-valued setter cannot represent. `main.c:169` already collides
   with this and papers over it in a comment: HTTP-start failure reuses
   `DEVICE_STATUS_WIFI_DOWN` because *"no dedicated 'API down' status exists yet."*
3. Neither fact is observable at runtime. `include/wifi_conn.h` exports only
   `wifi_conn_start()` — association state lives inside `wifi_conn.c`'s event
   handlers. `http_api_start()` holds its `httpd_handle_t server` in a **local
   variable** (`src/http_api.c:233`) and discards it on return.

So the work is: a pure policy core, atomic two-fact tracking reported from the
existing Wi-Fi event handlers, a thin WS2812 driver, and a tick task.

### Design decisions settled during brainstorming

- **Facts are tri-state** (`UNKNOWN` / `UP` / `DOWN`), not boolean. With a boolean,
  `http` initialises to "down" and the LED claims *"permanently broken, reflash
  me"* for the first second of every boot. `UNKNOWN` is what makes the boot window
  honest.
- **Precedence: `http == DOWN` outranks everything**, because it is permanent and
  unrecoverable without a reflash. This mirrors the precedence already set at
  `main.c:107` — *"LEVEL_FAULT is the more specific and more urgent condition, so
  it wins."*
- **Atomics, not a mutex.** Each fact has exactly one writer and there is no
  read-modify-write anywhere, so a mutex would buy nothing and would put a
  potentially-blocking acquire inside a Wi-Fi event handler. `reading_store` uses a
  mutex because it guards a 2,880-entry buffer; this guards two enums.
- **Vendor the IDF RMT encoder, do not add a managed component.**
  `espressif/led_strip` is not bundled in IDF (the framework tree has only
  `esp_driver_rmt` / `esp_driver_ledc`), so it would be a new registry dependency
  in a manifest that already carries a scar — `espressif/ds18b20` is pinned to
  0.3.1 because 0.4.0's `$CONFIG{}` clause crashes the vendored
  idf-component-manager. IDF ships
  `examples/peripherals/rmt/led_strip/main/led_strip_encoder.{c,h}` (124 + 36
  lines, Apache-2.0, SPDX headers intact) which drives WS2812 with **zero**
  dependencies. We drive one LED with three colours; the managed component buys
  nothing here.
- **`mem_block_symbols = 48`, not the example's 64.** Verified directly against the
  pinned IDF 5.3.1 driver source, not inferred: `esp_driver_rmt/src/rmt_tx.c:247-248`
  validates that the value is **even and ≥ `SOC_RMT_MEM_WORDS_PER_CHANNEL`** (48 on
  ESP32-S3, `soc_caps.h:269`), so 48 is the enforced legal minimum rather than a
  preference; and `rmt_tx.c:115-117` computes
  `mem_block_num = mem_block_symbols / 48` **rounding up**, which is exactly why 64
  consumes two memory blocks and 48 consumes one. The example is sized for a 24-LED
  strip; 24 bits plus a reset code fits one block. This matters because the 1-Wire
  bus already holds RMT channels and Phase 2 of the previous feature already paid for
  an orphaned-channel bug once.
- **Kconfig `choice`, not `int` + `range`.** Deviation D5 is `range 1 48` on four
  pin options where the `comment` block's prose constrains nothing. A `range`
  cannot express this SoC's disjoint legal set (1–21 and 38–48; 22–25 do not
  exist, 26–32 is SPI flash, 33–37 is octal PSRAM). A `choice` of {48, 47, 38}
  makes an invalid pin **unrepresentable** and encodes the 47-vs-48 documentation
  conflict in the menu itself.
- **LED task at priority 2.** The ESP-IDF main task runs at priority 1, so
  priority 2 **preempts `app_main`** — which is the only reason the LED can blink
  through the 1.1 s nominal / 3.3 s worst-case blocking boot sensor read
  (`systemPatterns.md` § TWDT). At priority 1 it would be starved during exactly
  the window a status indicator is most wanted. Sampler stays above at 5.

### Deliberate scope exclusions

- **Sensor health is not shown.** The LED reads green with both probes unplugged.
  This is an accepted trade, recorded so it is not "fixed" by accident — it is the
  LED analogue of the blank-chart bug fixed in `task/dashboard-empty-state-a11y`,
  and the user chose it knowingly. Sensor state stays a dashboard and log concern.
- **`status_set()` is untouched.** Its four states keep logging as they do.
  Unifying it with the new fact model is a separate refactor.
- **mDNS failure does not affect the LED.** Green means reachable at the IP; a
  failed `mdns_hostname_set()` breaks `hydroponics.local` while the IP still works.
- **D5 is not fixed wholesale.** The new GPIO option avoids repeating it; the four
  existing `range 1 48` options stay as they are. That is its own task.
- **Dark is not a state in the vocabulary.** A status indicator cannot indicate
  that it is itself broken, so dark means "powered off or LED failed — read the
  serial console."

## Specification

**Feature Type**: NFR/Infrastructure
**Creative Exploration Needed**: No — design fully settled in the approved brainstorm (Approved
Design Mode); GPIO 47-vs-48 vendor disagreement is resolved structurally by a Kconfig `choice`
(AC-VERIFY-6), not by further exploration.

### Verification Method

- **Test method**: `pio test -e native` runs the new `test/test_status_led_core` Unity suite
  (exhaustive 9-cell fact→state truth table, blink-phase edge cases, brightness-scaling edge
  cases, solid-state stability) against `lib/status_led_core/`, which has zero ESP-IDF/FreeRTOS
  dependency and needs no `test/native/esp_shim.h` include. `lib/status_led/`,
  `src/status_led_task.c`, and the extended `lib/device_status/` and `src/wifi_conn.c` device-only
  halves are not host-testable (no RMT peripheral or Wi-Fi radio on the host) and are verified by
  a documented bench procedure instead: `pio device monitor` for the serial state-transition log
  (Phase 2) and direct visual observation of the onboard RGB LED plus a clean rebuild
  (`rm -rf .pio/build && pio run -e esp32-s3-devkitm-1`) for the flash-delta check (Phase 3).
- **Success metrics**: correct presentation state (`GREEN_SOLID` / `RED_BLINK` / `RED_SOLID`) for
  all 9 `(wifi × http)` fact combinations, exhaustively asserted; first LED blink visible < 1 s
  after power-on (before `sampler_sensors_init()`'s boot sensor read, 1.1 s nominal / 3.3 s
  worst-case, completes); a Wi-Fi disconnect and a subsequent reconnect are each reflected on the
  LED within 2 s of the triggering `wifi_conn.c` event; RMT channel budget after this change is 2
  of 4 TX channels and 1 of 4 RX channels consumed (verified by grep for RMT channel-creation call
  sites: one in `lib/status_led/src/status_led.c`, one TX + one RX in the existing 1-Wire bus owned
  by `src/sampler.c`); native host-test count rises from 39 to approximately 59 (~20 new tests,
  per Test Strategy § Approach); Phase 1 produces a **zero** flash delta (the core has no firmware
  caller yet) and Phase 3 produces a **non-zero** flash delta on a clean rebuild.
- **Observable at**: the board's onboard addressable RGB LED (WS2812, GPIO 48 by Kconfig default,
  Kconfig-selectable to 47 or 38 — AC-VERIFY-6); the serial console via `pio device monitor` for
  the derived-state transition log and any `status_led_init()`/`status_led_show()` error; the
  `pio test -e native` console output for pass/fail of the pure-core suite.
- **Verification frequency**: the pure-core suite runs on every `pio test -e native` invocation
  going forward (continuous, host-only, no hardware needed). The bench procedure (8 steps,
  including the injected `http_api_start()` failure check — AC-INTEGRATION-1 — and a DS18B20
  RMT-interaction check confirming valid temperature reads continue while the LED is blinking) is
  a one-time verification performed once at the end of Phase 3, documented in Test Strategy §
  Per-Phase Test Guidance.

### Acceptance Criteria

#### AC-VERIFY-1: Green solid appears only when both facts are UP
**Priority**: MUST
**Given** `status_led_core`'s state-derivation function receives a `status_facts_t{wifi, http}`
**When** `wifi == STATUS_FACT_UP` **and** `http == STATUS_FACT_UP`
**Then** the derived state is `STATUS_LED_STATE_GREEN_SOLID`; for every one of the other 8
`(wifi, http)` combinations the derived state is never `GREEN_SOLID`

#### AC-VERIFY-2: Red blinking covers booting, connecting, and reconnecting
**Priority**: MUST
**Given** the same state-derivation function
**When** the fact pair is one of the 5 cells that are neither "both UP" (AC-VERIFY-1) nor
"`http == DOWN`" (AC-VERIFY-3) — namely `(wifi=UNKNOWN, http=UNKNOWN)`, `(wifi=UP, http=UNKNOWN)`,
`(wifi=DOWN, http=UNKNOWN)`, `(wifi=UNKNOWN, http=UP)`, and `(wifi=DOWN, http=UP)`
**Then** the derived state is `STATUS_LED_STATE_RED_BLINK` in exactly those 5 cells and no
others — covering boot (`wifi=UNKNOWN, http=UNKNOWN`), initial connect (`wifi=UNKNOWN/DOWN,
http=UP`), and reconnect after a drop (`wifi=DOWN, http=UP`)

#### AC-VERIFY-3: Red solid appears when, and only when, the HTTP fact is DOWN
**Priority**: MUST
**Given** the same state-derivation function
**When** `http == STATUS_FACT_DOWN`, for any value of `wifi`
**Then** the derived state is `STATUS_LED_STATE_RED_SOLID` in all 3 such combinations, and
`RED_SOLID` is never produced when `http != STATUS_FACT_DOWN` — matching the precedence already
established at `src/main.c:104-112` ("the more specific and more urgent condition ... wins")

#### AC-VERIFY-4: The pure core's 9-cell truth table is exhaustively host-tested and registered
**Priority**: MUST
**Given** `test/test_status_led_core/test_status_led_core.c` exercises all 9 `(wifi × http)`
combinations against the derivation function
**When** `pio test -e native` runs
**Then** every combination has its own `TEST_CASE`/`RUN_TEST` asserting the exact expected state
(no combination is skipped or asserted only implicitly), and `platformio.ini`'s `[env:native]`
`test_filter` (currently `test_reading_store, test_level_switches, test_sensor_hub,
test_wifi_backoff, test_reading_json` at line 127) includes `test_status_led_core` — an
unregistered suite compiles but never runs, per the existing convention

#### AC-VERIFY-5: Blink phase and brightness scaling are exhaustively host-tested at their edges
**Priority**: MUST
**Given** `status_led_core`'s blink-phase and brightness-scaling functions
**When** the host suite exercises: lit at tick 0, dark at `blink_ticks`, lit again at
`2×blink_ticks`, `blink_ticks == 1`, `uint32_t` tick wrap, `blink_ticks == 0` (must not divide by
zero); and brightness scaling at 255 (passes through unscaled), 1 (does not underflow to black),
and the `255×255` product (does not overflow `uint8_t`/`uint16_t` intermediate), with monotonicity
across the range
**Then** every one of these host tests passes, and solid-state frames (`GREEN_SOLID`,
`RED_SOLID`) return an identical RGB frame at every tick with no false "changed" transmit

#### AC-VERIFY-6: The LED GPIO comes from a Kconfig `choice`; no pin literal in source
**Priority**: MUST
**Given** the new `menu "Status LED (onboard RGB)"` in `src/Kconfig.projbuild` (alongside the
existing "Sampler" and "SNTP / Time" submenus) defines `HYDRO_STATUS_LED_GPIO` as a `choice` among
GPIO 48 (default), 47, and 38 — not an `int` + `range`
**When** `lib/status_led/src/status_led.c` and `src/status_led_task.c` are grepped for a numeric
GPIO literal
**Then** the only GPIO value used to configure the RMT TX channel is
`CONFIG_HYDRO_STATUS_LED_GPIO_*`-derived from the Kconfig choice; no bare `47`, `48`, or `38`
appears as a pin assignment in source, honoring Guiding Principle "Configuration Is Not
Hard-Coded" and avoiding a repeat of Known Deviation D5 (`src/Kconfig.projbuild:9,16,27,41,50`)
for this new option

#### AC-VERIFY-7: RMT channel budget and One-Owner-Per-Peripheral hold after this change
**Priority**: MUST
**Given** the firmware after Phase 3, with `status_led_init()` creating exactly one RMT TX channel
(`mem_block_symbols = 48`, the legal minimum per `SOC_RMT_MEM_WORDS_PER_CHANNEL` on ESP32-S3,
`soc_caps.h:269` — not the vendored example's 64) and the existing 1-Wire bus (owned by
`src/sampler.c`, via `onewire_new_bus_rmt()`) as the only other RMT consumer. Verified directly
against the pinned IDF 5.3.1 driver source: `esp_driver_rmt/src/rmt_tx.c:247-248` validates that
`mem_block_symbols` must be even and **≥ 48** (`SOC_RMT_MEM_WORDS_PER_CHANNEL`), so 48 is the
legal minimum, not merely a size preference; `esp_driver_rmt/src/rmt_tx.c:115-117` computes
`mem_block_num = mem_block_symbols / 48`, rounding **up** when it doesn't divide evenly — which is
why the vendored example's `mem_block_symbols = 64` consumes **two** memory blocks while this
project's `48` consumes exactly one
**When** the built firmware is audited by grepping for RMT channel-creation call sites
(`rmt_new_tx_channel`/`onewire_new_bus_rmt`)
**Then** exactly 2 of the ESP32-S3's 4 RMT TX channels and exactly 1 of its 4 RX channels are
consumed project-wide; `status_led_init()` is the sole and single-call creator of its channel (no
second `status_led_init()` call site anywhere); this matches `SOC_RMT_TX_CANDIDATES_PER_GROUP 4` /
`SOC_RMT_RX_CANDIDATES_PER_GROUP 4` (`soc_caps.h:266-267`)

#### AC-VERIFY-8: HYDRO_STATUS_LED_ENABLE=n cleanly disables the feature with no partial acquisition
**Priority**: SHOULD
**Given** `HYDRO_STATUS_LED_ENABLE` (bool, default `y`) is set to `n` via `menuconfig`
**When** the firmware is rebuilt and `status_led_start()` runs during boot
**Then** the build succeeds; `status_led_start()` returns `ESP_OK` without creating its FreeRTOS
tick task and without calling `status_led_init()` (so no RMT TX channel is acquired — the
project-wide RMT budget stays at 1 of 4 TX / 1 of 4 RX, the 1-Wire bus only, per AC-VERIFY-7); no
`status_led_*` error is logged; and boot otherwise proceeds unchanged from the enabled case. This
matters because the GPIO choice in AC-VERIFY-6 rests on vendor Q&A rather than a confirmed
schematic, so cleanly turning the feature off is a real supported path, not a formality

#### AC-ASYNC-1: The LED shows first illumination under 1 second after power-on, before the boot sensor read completes
**Priority**: MUST
**Given** `status_led_start()` is wired early in `app_main()` — immediately after the
`esp_get_free_heap_size()` log at `src/main.c:65` and before `sampler_sensors_init()` at
`src/main.c:72` — with its FreeRTOS task created at priority 2 (above the ESP-IDF main task's
priority 1, so it preempts `app_main()`)
**When** the board is power-cycled and the boot sensor read (1.1 s nominal, up to 3.3 s
worst-case per `systemPatterns.md` § Task Watchdog) is still in progress
**Then** the onboard LED is observed **illuminated red** within 1 second of power-on — in
practice within the first ~100 ms tick — observable on the bench before the boot read's log lines
appear on the serial console. This asserts first illumination only, not a completed blink edge:
the subsequent lit/dark **cadence** is governed by `HYDRO_STATUS_LED_BLINK_MS` (Kconfig `range
100 2000`, default 500) and is deliberately outside this AC's threshold, since a legal
configuration value (e.g. 2000 ms) would otherwise make a correctly-built firmware fail a
blink-edge-based assertion. First illumination still exercises the same priority-2-preempts-
`app_main()` behavior this AC exists to verify

#### AC-ASYNC-2: A Wi-Fi drop and its recovery are each reflected on the LED within 2 seconds
**Priority**: MUST
**Given** the device is in steady-state `GREEN_SOLID` (Wi-Fi associated, HTTP server running) and
`wifi_conn.c`'s `WIFI_EVENT_STA_DISCONNECTED` branch (`src/wifi_conn.c:134-139`) and
`IP_EVENT_STA_GOT_IP` handler (`src/wifi_conn.c:142-162`) each call the new `device_status`
`report_wifi()` writer with `STATUS_FACT_DOWN` / `STATUS_FACT_UP` respectively
**When** the AP is power-cycled (triggering `WIFI_EVENT_STA_DISCONNECTED`) and later comes back
(triggering `IP_EVENT_STA_GOT_IP`)
**Then** the LED transitions to `RED_BLINK` within 2 seconds of the disconnect event and back to
`GREEN_SOLID` within 2 seconds of the reconnect event, given the tick task's 100 ms poll period

#### AC-ERROR-1: Any LED init or task-creation failure logs and lets boot continue
**Priority**: MUST
**Given** `status_led_init()` returns a non-`ESP_OK` `esp_err_t` (e.g. RMT channel allocation
failure), or the subsequent `xTaskCreatePinnedToCore()` call in `status_led_task.c` returns
anything other than `pdPASS`
**When** `app_main()`'s early `status_led_start()` call observes this failure
**Then** an `ESP_LOGE` line is emitted naming the failure (via `esp_err_to_name()` for the
`esp_err_t` case), `status_led_start()` returns the error, and `app_main()` continues booting
exactly as it does for a `sampler_start()` or `http_api_start()` failure today
(`src/main.c:122-132`, `157-170`) — the device is fully functional with a dark LED, per Fail-Safe
Defaults

#### AC-ERROR-2: A repeating status_led_show() failure logs once, not once per tick
**Priority**: MUST
**Given** the 100 ms tick loop in `src/status_led_task.c` calls `status_led_show()` only when the
computed frame differs from the last one sent (transmit-only-on-change)
**When** `status_led_show()` returns a non-`ESP_OK` `esp_err_t` on multiple consecutive changed
ticks
**Then** the **first** failure is logged at `ESP_LOGE` with `esp_err_to_name()`; subsequent
consecutive failures are counted and suppressed from individual logging; a single `ESP_LOGI`
summary line reporting the suppressed count is emitted once the next successful `status_led_show()`
call recovers — a 10 Hz worst-case loop must never flood the log and bury sensor output

#### AC-INTEGRATION-1: The RED_SOLID (HTTP-down) branch is confirmed against real hardware, not only the truth table
**Priority**: MUST
**Given** the Phase 3 bench procedure builds a scratch firmware image in which
`http_api_start()`'s call site in `app_main()` (`src/main.c:157`) is patched to force a return of
`ESP_FAIL` regardless of the real `httpd_start()` result
**When** that scratch image is flashed to the physical ESP32-S3-N16R8 board and observed
**Then** the onboard LED is directly observed showing `RED_SOLID` on real hardware — closing the
gap between "the pure core's truth table says RED_SOLID is correct for this fact combination" and
"the WS2812 driver, RMT encoder, and byte order actually render it correctly on the physical LED,
including confirming GRB (not RGB) byte order, since a swapped order would self-reveal by showing
red for the reachable case instead"

### Scope Boundaries

- **In scope**: a pure policy core (`lib/status_led_core/`) deriving one of three presentation
  states from two tri-state facts, with blink-phase and brightness-scaling arithmetic, fully
  host-tested; extension of `lib/device_status/` with two `atomic_int` facts, `report_wifi()` /
  `report_http()` writers, and a `snapshot()` reader (existing `status_set()` logging untouched);
  wiring the `wifi` fact from the two existing event handlers in `src/wifi_conn.c` and the `http`
  fact once from `http_api_start()`'s return value in `src/main.c`; a thin WS2812 driver
  (`lib/status_led/`) using a vendored, unmodified IDF RMT encoder; a 100 ms tick task
  (`src/status_led_task.c`) at priority 2 wired early in `app_main()`; Kconfig for
  enable/GPIO-choice/brightness/blink-rate.
- **Out of scope**: sensor health represented on the LED (deliberate exclusion — the LED reads
  green with both probes unplugged, a trade-off the user was shown and chose); refactoring or
  unifying `status_set()`/`device_status_t`'s four existing states with the new fact model; mDNS
  registration failure affecting the LED (green means reachable at the raw IP even if
  `hydroponics.local` fails to resolve); fixing Known Deviation D5 for the four *existing*
  `range 1 48` Kconfig pin options (`src/Kconfig.projbuild:9,16,27,41,50`) — only the new GPIO
  option avoids repeating it; any actuator/relay work; OTA; dynamic/adaptive brightness.
- **Dependencies**: `src/wifi_conn.c`'s existing `WIFI_EVENT_STA_DISCONNECTED`
  (`wifi_event_handler`, lines 120-140) and `IP_EVENT_STA_GOT_IP` (`ip_event_handler`, lines
  142-162) handlers as the sole `wifi`-fact writer sites; `src/main.c`'s existing
  `http_api_start()` call (line 157) as the sole `http`-fact writer site; the vendored
  `led_strip_encoder.c`/`.h` (124 + 36 lines, Apache-2.0, from
  `~/.platformio/packages/framework-espidf/examples/peripherals/rmt/led_strip/main/`); the
  existing `[env:native]` host-test harness (`test/native/esp_shim.h`) for the pure core, though
  `status_led_core` needs none of its ESP-IDF shims since it defines its own fact/state types.
- **NFR implications**: no persisted data and no network-facing surface — the LED is a purely
  local, ambient hardware indicator with zero security or privacy exposure; the only performance
  requirement is the sub-1s/sub-2s latency targets in the Verification Method above; RMT resource
  budget (2 of 4 TX, 1 of 4 RX) is the binding scalability constraint on this SoC, per Guiding
  Principle "One Owner Per Peripheral".

### Implementation Guide Required

No — this is a firmware change fully realized through the project's existing `/bmb:build` →
flash → bench-verify workflow. The bench procedure (7 steps, including the injected
`http_api_start()` failure check) is already documented in the Test Strategy section's §
Per-Phase Test Guidance for Phase 3; no separate step-by-step realization guide is needed beyond
that.

## User Journey Definition

**Feature Type**: NFR/Infrastructure

*Note: user-observable hardware output, but there is no invocation element and no
persisted data — the indicator is ambient. Verification is by bench observation
plus host tests, so the NFR path is the honest classification.*

**Creative Phase Required**: Yes - Architecture (complete — see
`memory-bank/creative/onboard-status-led-design.md`)

### NFR Verification (Infrastructure Features)

- **Test method**: `pio test -e native` for the pure policy core (exhaustive
  truth table, blink phase, brightness scaling); documented bench observation on
  the ESP32-S3 for the driver, task, and RMT paths, which cannot be host-tested.
- **Success metrics**: correct presentation state for all 9 fact combinations;
  first blink visible < 1 s after power-on (before the boot sensor read
  completes); state transition reflected on the LED < 2 s after the triggering
  event; RMT budget after this change = 2 of 4 TX, 1 of 4 RX; native suite count
  39 → ~59.
- **Observable at**: the board's onboard RGB LED (GPIO 48), plus the serial
  console (`pio device monitor`) for the derived-state transition log and any
  driver error.

### Acceptance Criteria

- AC-VERIFY-1: green solid appears only when both facts are UP
- AC-VERIFY-2: red blinking covers booting, connecting, and reconnecting
- AC-VERIFY-3: red solid appears when, and only when, the HTTP fact is DOWN
- AC-VERIFY-4: the pure core's 9-cell truth table is exhaustively host-tested and
  the suite is registered in `[env:native]`'s `test_filter`
- AC-VERIFY-5: the LED GPIO comes from a Kconfig `choice`; no pin literal in source
- AC-ASYNC-1: the LED is blinking < 1 s after power-on, before the boot sensor
  read completes
- AC-ASYNC-2: a Wi-Fi drop and recovery are reflected on the LED within 2 s each way
- AC-ERROR-1: any LED init or task-creation failure logs and lets boot continue —
  the device is fully functional with a dark LED
- AC-ERROR-2: a repeating `status_led_show()` failure logs once, not once per tick

## Test Strategy

### Approach

- **Emphasis**: unit-heavy on the pure core, per `systemPatterns.md` § Test Scope
  Preferences ("when a module has a pure half, the core is 100% unit-tested on
  `[env:native]`; the wrapper is device-only and not host-tested").
- **Target test count**: ~20 new host tests (39 → ~59), against ~60–80 SLOC of
  pure logic — a ratio of ~0.3, in line with the project's ~0.4–0.5 aim.

### File Organization

- **New test files**: `test/test_status_led_core/test_status_led_core.c` — state
  derivation (all 9 fact combinations, exhaustively), blink phase (lit at tick 0,
  dark at `blink_ticks`, lit at `2×blink_ticks`, `blink_ticks == 1`, `uint32_t`
  tick wrap, and `blink_ticks == 0` must not divide by zero), brightness scaling
  (255 passes through, 1 does not underflow to black, `255×255` does not overflow,
  monotonic), and solid-state stability (green and solid red return an identical
  frame at every tick).
- **Extend existing**: none. `platformio.ini`'s `[env:native]` `test_filter` must
  gain `test_status_led_core` — it lists suites **explicitly**, so an unregistered
  suite compiles and never runs.

### What NOT to Test

- RMT transactions and WS2812 bit timing — no physical bus on the host; bench-verified
- Perceived colour and GRB byte order — physical; the bench check catches a swap
  immediately, since "reachable" would render red
- Wi-Fi association / disconnection events — require a real AP (existing exclusion)
- FreeRTOS task scheduling and preemption — assumed correct from IDF; the
  priority-2 choice is verified behaviourally at the bench (first blink < 1 s)
- `lib/status_led/` and `src/status_led_task.c` — device-only halves, not
  host-tested by the project's own split convention

### Per-Phase Test Guidance

- **Phase 1**: ~20 host tests, as enumerated above. **Expect no flash delta** —
  the core's only caller is the test binary, so it correctly does not appear in
  the firmware image yet. The `build-verification` learned rule's flash-delta
  check applies at Phase 3, not here.
- **Phase 2**: no new host tests (the plumbing is device-only). Verified on
  serial: `BOOT → CONNECTING → REACHABLE`, then `REACHABLE → RECONNECTING` on AP
  power-cycle. **The log line's owner is `device_status`'s `report_wifi()` /
  `report_http()` writers**, which derive the state via `status_led_core_state()`
  and log on *change only*. This is a deliberate assignment, not an accident of
  convenience: `src/status_led_task.c` does not exist until Phase 3, so without a
  named owner here Phase 2 has nothing to observe and stops de-risking anything.
  It creates a `lib/device_status` → `lib/status_led_core` dependency, which is
  device → pure and therefore the correct direction. Do **not** relocate this
  derivation into `src/` — that would make it Phase-3-only.
- **Phase 3**: no new host tests. Clean rebuild (`rm -rf .pio/build`) with a
  **non-zero** flash delta, then the 8-step bench procedure — including the
  injected-fault check that forces `http_api_start()` to return `ESP_FAIL` in a
  scratch build, so the `RED_SOLID` branch is seen on hardware rather than
  shipping unobserved, and the RMT-interaction check below.

  **Bench step added by the plan critique — confirm the DS18B20 still reads while
  the LED is blinking.** Choosing priority 2 (§ Design decisions) makes the LED
  task *preempt* `app_main`, and the LED and the 1-Wire bus are now both RMT
  consumers, so the LED task can interrupt a 1-Wire transaction in a way the
  pre-existing design could not. Assessed risk is **low** — RMT waveforms are
  generated by per-channel hardware rather than by the CPU, the channels are
  independent, and transmit-on-change costs roughly 80 µs twice a second — but
  "low" is an argument, not evidence, and the previous feature already lost an
  entire lux series to an unnoticed RMT interaction. Confirm a valid
  `water temperature: NN.NN C` line on serial with the LED actively blinking.

## Implementation Roadmap

### New Source Files (pin path + extension)

- [ ] `lib/status_led_core/include/status_led_core.h` — pure: fact types, state enum, RGB struct, API
- [ ] `lib/status_led_core/src/status_led_core.c` — pure: state derivation, blink phase, brightness
- [ ] `lib/status_led/include/status_led.h` — device-only driver API (`init`, `show`)
- [ ] `lib/status_led/src/status_led.c` — device-only: RMT TX channel + frame transmit
- [ ] `lib/status_led/src/led_strip_encoder.c` — vendored from IDF example, Apache-2.0, unmodified
- [ ] `lib/status_led/include/led_strip_encoder.h` — vendored from IDF example, Apache-2.0, unmodified
- [ ] `src/status_led_task.c` — device-only: 100 ms tick loop, `xTaskDelayUntil`
- [ ] `include/status_led_task.h` — `status_led_start(void)` declaration
- [ ] `test/test_status_led_core/test_status_led_core.c` — Unity host suite
- [ ] extend `lib/device_status/include/device_status.h` — fact reporters + snapshot
- [ ] extend `lib/device_status/src/device_status.c` — two atomics, unchanged `status_set()`
- [ ] extend `src/main.c` — early `status_led_start()`; report the `http_api_start()` result
- [ ] extend `src/wifi_conn.c` — report the wifi fact from both existing event handlers
- [ ] extend `src/Kconfig.projbuild` — `menu "Status LED (onboard RGB)"`
- [ ] extend `platformio.ini` — add `test_status_led_core` to `[env:native]` `test_filter`

### Phases

- [x] Phase 1: Pure policy core + exhaustive host tests (no hardware, no firmware behaviour change)
- [x] Phase 2: Two-fact plumbing, observable on serial (no LED yet)
- [x] Phase 3: WS2812 driver, tick task, Kconfig, clean rebuild + bench verification

## Creative Phases

- [x] Architecture design → complete (`memory-bank/creative/onboard-status-led-design.md`)

## Plan Critique

**Backend**: `anthropic` — seam resolution outcome `configured:anthropic`
(`backends.creative-critique: anthropic` read from `main:memory-bank/projectConfig.md`;
step 1 of use-time resolution is terminal for an `anthropic` value, so no Codex probe
was required). Per `projectConfig.md`'s own note, this is a **same-provider
self-critique** and is structurally weaker than an independent reviewer — it can catch
contradictions and unstated assumptions, but it is not an outside opinion. If the
`codex@openai-codex` plugin is installed later, this seam should go back to `codex`.

**Verdict**: proceed with remediation applied. No finding invalidated an approved
design decision, so nothing needed to go back to the user.

**Scope**: `tasks/onboard-status-led.md` (description, phases, `## Specification`),
`roadmap/onboard-status-led.md`, `creative/onboard-status-led-design.md`.

| # | Severity | Finding | Disposition |
|---|---|---|---|
| 1 | high | **`AC-ASYNC-1`'s threshold was ambiguous and Kconfig-falsifiable.** "First `RED_BLINK` transition within 1 s" reads either as entering the state (< 100 ms) or as a lit→dark blink edge (`HYDRO_STATUS_LED_BLINK_MS`, default 500 ms). Read as a blink edge, a legal `BLINK_MS` of 2000 breaks a MUST-priority AC on a correctly-built firmware. | **applied** — Spec Writer re-dispatched; AC now asserts *first illumination* and states explicitly that blink cadence is `BLINK_MS`-dependent and excluded from the threshold |
| 2 | medium | **No AC covered `HYDRO_STATUS_LED_ENABLE=n`.** The disable path is a deliberate escape hatch (the GPIO rests on vendor Q&A, not a schematic), so it is a supported path, and it was untested. | **applied** — `AC-VERIFY-8` added (`SHOULD`): builds, returns `ESP_OK`, no task, no RMT channel acquired, budget reverts to 1 TX / 1 RX, no error logged |
| 3 | medium | **Phase 2's log line had no named owner.** `src/status_led_task.c` does not exist until Phase 3, so "observable on serial" had nothing producing it — Phase 2 would have stopped de-risking anything. | **applied** — assigned to `device_status`'s `report_*()` writers deriving via `status_led_core_state()`, logging on change only; the resulting `device_status` → `status_led_core` dependency is device → pure and recorded as correct |
| 4 | medium | **Priority 2 creates a new RMT-interaction risk.** Preempting `app_main` means the LED task can now interrupt a 1-Wire transaction; both are RMT consumers. Risk is low (per-channel hardware waveform generation, independent channels, ~80 µs twice a second) but the previous feature already lost a whole lux series to an unnoticed RMT interaction. | **applied** — 8th bench step added: confirm a valid `water temperature` line on serial with the LED actively blinking |
| 5 | high *(if true)* | **`mem_block_symbols = 48` might be rejected by the RMT driver**, which would have shipped as a Phase 3 build failure. | **refuted** — checked against pinned IDF 5.3.1: `rmt_tx.c:247-248` requires only *even and ≥ 48*, so 48 is the legal minimum; `rmt_tx.c:115-117` confirms 64 rounds up to two blocks. The design was right; citations upgraded from reasoned to verified |
| 6 | low | **`AC-VERIFY-2`'s `When` clause was a hard-to-parse double negative.** Verified logically correct (it selected exactly the intended 5 cells), so this was readability only. | **applied** — reworded to name the 5 `(wifi, http)` cells directly; selected cells unchanged |
| 7 | low | **`HYDRO_STATUS_LED_BLINK_MS` has silent 100 ms granularity** — `blink_ticks = BLINK_MS / 100` integer-divides, so 150 ms becomes 100 ms. Kconfig cannot validate "multiple of 100". | **noted** — to be stated in the option's `help` text at build time; recorded in the creative doc |
| 8 | low | **Red and green are not equally bright at the same numeric scale** — green LEDs read considerably brighter, so one 32/255 default may suit one colour and not the other. | **noted** — a per-colour scale is a one-line change in the pure core and fully host-testable there; not worth pre-solving |

Both `noted` findings are low severity and advisory for build. All high/medium findings
ended `applied`; none required `user-decided`. The taxonomy lint and concrete-spec gates
were re-run after every spec re-dispatch and after each direct edit — **CLEAN** each time
(13 ACs; all prefixes canonical; 13/13 with `Priority` + Given/When/Then; all four
`Verification Method` fields present).

---

## Execution State

## Reflect / Archive Execution State

**Build Status**: IDLE
**Current Phase**: COMPLETE
**Can Resume**: NO
**Current Step**: Archived 2026-08-21 — `memory-bank/archive/onboard-status-led-archive.md`

**Task Quality**: Strong / Partial Success (hardware-gated MUST ACs implemented-but-undemonstrated)
**Ecosystem Effectiveness**: Highly Effective

### Completed Steps (REFLECT)
- Step 0 v1 Guard + Resolve Task Reference: COMPLETE (v2 structure; slug `onboard-status-led`; task file present on feature/onboard-status-led tip)
- Step 0.1 Sync-Before-Resume: COMPLETE (feature branch 4 ahead / 0 behind origin/main — no rebase needed)
- Step 0.2 Interrupted-Reflection Check: COMPLETE (no prior REFLECT state — new reflection)
- Step 0.3 Phase Gate: PASS (status BUILD_COMPLETE; creative reference `memory-bank/creative/onboard-status-led-design.md` verified present)
- Step 1 Verify Prerequisites: COMPLETE (all 3 implementation phases `[x]`)
- Step 2 Load Complexity Context: COMPLETE (Level 3 → level3-reflection.md)
- Step 3 Reflection Agent (Sonnet): COMPLETE — Output: `memory-bank/reflection/onboard-status-led-reflection.md`. Task Quality: Strong / Partial Success; Ecosystem Effectiveness: Highly Effective. 3 extractable learnings captured in the doc (testing-patterns, acceptance-criteria, test-strategy) — NOT written to `agent-rules/_learned/` (that consolidation happens at `/bmb:archive`).
- Step 4 Git Commit: COMPLETE

### Open Item Carried to Archive
**Bench verification PARTIAL (2026-08-21)** — first hardware boot on the physical ESP32-S3
confirmed 4 of 6 items. Reported observation: *"LED worked like a charm on boot. Flashed red,
went to green when Wi-Fi was connected."*

**Confirmed on hardware:**
- `RED_BLINK` and `GREEN_SOLID` both render as intended (visual, 2 of 3 states)
- **GPIO 48 is correct** — the vendor Q&A answer, not the vendor-documented GPIO 47. The Kconfig `choice` stays as-is; 47/38 are no longer needed.
- **GRB byte order is correct** — red rendered as red and green as green, so no red/green channel swap
- **AC-ASYNC-1** — illumination present at boot, before the blocking sensor read. Observed qualitatively ("on boot"), not stopwatch-measured against the 1 s threshold; the load-bearing `status_led_start()`-before-`sampler_sensors_init()` ordering is confirmed working.
- **AC-ASYNC-2, forward direction only** — Wi-Fi association drove `RED_BLINK` → `GREEN_SOLID`

**Still outstanding:**
- **AC-INTEGRATION-1** — `RED_SOLID` has never been observed. Needs an injected `http_api_start()` failure (recommended method: temporarily set `config.max_uri_handlers = 0` in `src/http_api.c`, flash, observe, revert — do not commit).
- **AC-ASYNC-2, reverse direction** — a Wi-Fi *drop* returning the LED to `RED_BLINK` within 2 s was not observed (only the connect direction was)
- **RMT coexistence** — DS18B20 1-Wire reads succeeding while the LED blinks. Blocked until the temperature probe is physically installed; the DS18B20 holds an RMT TX+RX pair and the LED holds one RMT TX (2 of 4 TX, 1 of 4 RX).

The reflection flags this as the 2nd consecutive occurrence (after `sensor-monitoring-dashboard`) of a task closing with undemonstrated hardware-gated criteria, and raises it as a High Priority ecosystem gap: BMB needs a first-class bench-verification status rather than prose in Resumption Notes.

---

## Build Execution State

**Build Status**: COMPLETE
**Current Build**: Phase 3: WS2812 driver, tick task, Kconfig, clean rebuild + bench verification (onboard-status-led) — FINAL PHASE
**Build Started**: 2026-08-21T16:10:00Z
**Phase Number**: 3 of 3
**Is Multi-Phase**: YES

### Current Build Step
**Step**: Step 11 - Phase Git Completion
**Status**: COMPLETE
**Started**: 2026-08-21T16:10:00Z
**Completed**: 2026-08-21T16:55:00Z
**Output**: Phase 3 (final phase) committed and pushed to feature/onboard-status-led

### Completed Steps
- Step 0.5 Git Setup: COMPLETE (inline — main checkout already on feature/onboard-status-led, no separate worktree needed)
- Step 0.6 Phase Gate: COMPLETE (Implementation Roadmap populated; Architecture creative phase complete)
- Step 1 Read Task Context: COMPLETE (Phase 3 of 3, final phase, identified)
- Step 2 Load Complexity Context: COMPLETE (Level 3)
- Step 3 TDD Agent: COMPLETE — device-only phase, zero new host tests by design (per Test Strategy: RMT driver + FreeRTOS task cannot run under [env:native]); regression gate substituted for RED/GREEN (71/71 native tests unchanged before and after). New: lib/status_led/{include,src}/status_led.{h,c} (RMT TX channel, mem_block_symbols=48, GRB byte order, Kconfig-choice GPIO resolution), lib/status_led/{include,src}/led_strip_encoder.{h,c} (vendored verbatim from ESP-IDF example, Apache-2.0, diff-confirmed byte-identical), include/status_led_task.h + src/status_led_task.c (100ms tick task, priority 2, transmit-only-on-change, AC-ERROR-2 suppressed-failure-count logging, AC-VERIFY-8 clean-disable path). Extended: src/main.c (early status_led_start() before sampler_sensors_init(), AC-ASYNC-1 ordering), src/Kconfig.projbuild (new "Status LED (onboard RGB)" menu: HYDRO_STATUS_LED_ENABLE/_GPIO choice/_BRIGHTNESS/_BLINK_MS), platformio.ini (status_led added to esp32-s3-devkitm-1 lib_deps only).
- Step 7 Integration Verification (bmb:build-verifier-agent): COMPLETE — tests PASS 71/71 (unchanged, no new host tests this phase by design); device build PASS (RAM 32.6% / 106,708 B unchanged, Flash 30.7% / 965,632 B, non-zero delta confirming the core's first firmware caller); lint: 1 new LOW cppcheck "unusedFunction" false positive on status_led_start (same class as 9 pre-existing false positives on cross-file-called functions; confirmed status_led_start() IS called at src/main.c:74) — accepted as baseline noise, not a regression, consistent with the project's already-documented cppcheck limitation for this build configuration.
- Step 8 Code Review: COMPLETE — APPROVED, 0 blocking. AC-VERIFY-6/7/8, AC-ERROR-1/2, GRB byte order, priority/stack, transmit-only-on-change, and sole-init-call-site all verified PASS. 1 RECOMMENDED finding (RMT channel/encoder not released on status_led_init()'s rmt_new_led_strip_encoder/rmt_enable failure branches) applied directly by the orchestrator (added rmt_del_channel()/encoder->del() cleanup on those error paths; device build re-verified SUCCESS post-fix, RAM/flash unchanged). 1 OPTIONAL finding (stack-size comment precision, already caveated as bench-unconfirmed) left as-is. Security PASS (no user input/network surface touched; 0 new external dependencies).
- Step 9 Documentation Agent: COMPLETE — techContext.md status banner + Recent Technology Changes updated (lib/status_led module, vendored encoder provenance, RMT budget table, updated build metrics, Kconfig menu, error-path cleanup note); systemPatterns.md deliberately left unchanged (this phase is the Nth application of the already-documented Pure-Logic/Device-Only Split pattern, not a new pattern — same reasoning precedent as Phase 2).
- Step 10 Memory Bank Update: COMPLETE — Phase 3 checkbox marked [x] (all 3 phases now complete), task status PLANNING_COMPLETE → BUILD_COMPLETE, Execution State updated.

### Sub-Agents
- TDD Agent (Sonnet, anthropic backend): COMPLETE — lib/status_led/ (NEW), include/status_led_task.h + src/status_led_task.c (NEW), src/main.c + src/Kconfig.projbuild + platformio.ini extended
- Integration Verifier Agent (Haiku): COMPLETE — verdict tests/build PASS, lint 1 accepted false-positive (see above); overall accepted as PASS by the orchestrator per the documented cppcheck limitation
- Code Reviewer Agent (Sonnet): COMPLETE — APPROVED (1 recommended finding applied directly by orchestrator: RMT resource cleanup on status_led_init() error paths)
- Documentation Agent (Haiku): COMPLETE — techContext.md updated; systemPatterns.md explicitly left unchanged with reasoning

### Guard & Recovery Log
(none — commit guard passed on first run)

### Resumption Notes
**Can Resume**: NO
**Resume From**: N/A — all 3 phases complete
**Notes**: All implementation phases of onboard-status-led are now complete and committed. Physical
bench verification (visual LED observation across all 3 states, the injected http_api_start()
failure check for RED_SOLID, GPIO/byte-order confirmation, and the DS18B20-reads-while-blinking
RMT-coexistence check — 8 steps documented in this file's Test Strategy § Per-Phase Test
Guidance, Phase 3) is a one-time human action requiring the physical ESP32-S3 board and is
explicitly out of scope for any build agent — it was not attempted here. Next actions: a human
performs the bench procedure (flash + `pio device monitor` + visual observation), then
`/bmb:reflect onboard-status-led` to capture learnings, then `/bmb:archive onboard-status-led`.

### Legacy Completed Steps (pre-build)
- Brainstorm conversation → design approved by user section-by-section (2026-08-20)
- Roadmap feature created: `onboard-status-led` (Level 3, inferred — not prompted)
- Task file authored; `## Specification` delegated to the Spec Writer Agent (Sonnet)
- Creative design doc written
- BRAINSTORM CRITIQUE: anthropic — `configured:anthropic` (8 findings: 5 applied,
  2 noted, 1 refuted; see § Plan Critique)
- TAXONOMY LINT: CLEAN (re-run after each spec re-dispatch and direct edit)
- CONCRETE-SPEC GATE: PASS (NFR path — Test method / Success metrics / Observable at /
  Verification frequency all concrete)
- GLOSSARY: not built (`main:memory-bank/c4/` absent) — soft loader skipped
