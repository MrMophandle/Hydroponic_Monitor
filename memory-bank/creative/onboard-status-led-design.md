# Creative Design — onboard-status-led

**Task**: `onboard-status-led`
**Feature**: `onboard-status-led`
**Design type**: Architecture
**Produced by**: `/bmb:brainstorm` (conversational design)
**Date**: 2026-08-20
**Status**: Approved by user

---

## Context

v1 of the firmware is feature-complete: six phases, 54 host tests, a LAN-hosted dashboard at
`hydroponics.local`. But the device is headless, always-on, and sits in a grow area — and its
only failure signal today is *"the page doesn't load."* That single symptom covers Wi-Fi never
associating, Wi-Fi dropping, the HTTP server failing to start, a crash, and a dead power
supply, with no way to tell them apart short of plugging in a serial cable.

The user's opening framing: *"I have an onboard RGBLED, I've seen it cycle through RGB when
the board was powered with no firmware loaded. I'd like to use that to display firmware
status (green = wifi connected, http server running, red = one of those things is not
happening)."*

Two things made this cheaper than it looked, and one thing made it more expensive.

**Cheaper:** the consumer seam already exists. `lib/device_status/` was built in Phase 2 of
`sensor-monitoring-dashboard` for exactly this purpose, and its header states the blocker
verbatim — *"the Hosyond ESP32-S3-WROOM-1's onboard status LED availability/GPIO is
unconfirmed … the hardware consumer is deferred until that pin is confirmed on the bench."*
Mid-conversation the user found the pin in the vendor's Amazon Q&A: *"the onboard LED appears
to be on GPIO48 (not GPIO47 as some docs or examples suggest)."* The documented blocker was
resolved, not assumed.

**More expensive:** the runtime status model the LED needs does not exist. `status_set()` is
called from five sites, all in `src/main.c`, all during boot; it stores nothing, exposes no
getter, and cannot represent a conjunction of two facts. `include/wifi_conn.h` exports only
`wifi_conn_start()`. And `http_api_start()` keeps its `httpd_handle_t server` in a local
variable (`src/http_api.c:233`) and discards it. The LED was the easy half.

---

## Decision 1 — What the LED is allowed to say

The user's phrasing implied two states. But an LED that encodes only reachability glows
**green with both sensors unplugged** — structurally the same defect as the blank-canvas bug
fixed the same day in `task/dashboard-empty-state-a11y`, where an empty chart was "visually
identical to a chart that failed to render."

**Options considered**

| Option | Vocabulary | Cost |
|---|---|---|
| Connectivity + sensor health | 6 states, spends 3–4 colours | Larger state machine; couples the LED to `sensor_hub` escalation |
| Connectivity, sensors as a blink modifier | 4 states, keeps two colours | Overloads blink, which is already needed for "connecting" |
| **Connectivity only** | 2 facts | Zero; sensor faults stay invisible on the LED |

**Decision: connectivity only.** The consequence was put to the user explicitly before they
chose, and they chose it knowingly. Recorded here because a future reader will notice the gap
and be tempted to "fix" it: **that is not a bug.** Sensor health remains a dashboard and log
concern, where it already has a badge and an offline escalation path.

The second option was rejected for a specific reason rather than on taste: blink is already
load-bearing for Decision 2, and encoding two independent meanings in one channel would make
both unreadable.

---

## Decision 2 — What red means during a normal boot

Red is the *default* state at power-on: association takes seconds, and `lib/wifi_backoff/`
retries on an escalating delay. A solid red therefore cannot distinguish *"three seconds into
a normal boot"* from *"wrong Wi-Fi password, will never connect."* An indicator that shows a
fault colour on every successful boot trains its user to ignore that colour.

The two facts also behave differently, which the vocabulary should reflect. `http_api_start()`
either succeeds once at boot or does not, and cannot stop later. Wi-Fi genuinely flaps.

**Options considered**

| Option | Result | Cost |
|---|---|---|
| Solid red, no distinction | 2 states; red on every boot | Red becomes noise |
| Dark until decided | Nothing to misread | Dark is also what a dead board looks like |
| Third colour for "connecting" | Most legible | Spends a colour; grows past the two-fact model |
| **Blinking red while trying, solid red for permanent** | 3 states, still 2 colours | Needs a blink timer |

**Decision: blinking red while trying; solid red reserved for permanent failure.** It keeps
the user's two-colour model, distinguishes boot noise from real failure, and gives the
otherwise-unused "solid red" a precise meaning: the HTTP server never started, which needs a
reflash rather than patience. The blink rate is constant — it deliberately does **not** encode
backoff depth, which would be unreadable and would couple the LED to `wifi_backoff` internals.

---

## Decision 3 — The fact model must be tri-state

A direct consequence of Decision 2 and the sharpest catch of the conversation.

With two booleans, `http` initialises to `false`, and the policy's `http == DOWN → RED_SOLID`
rule fires before `http_api_start()` has even run. The LED would claim *"permanently broken,
reflash me"* for the first second of **every** boot — the exact class of lie Decision 2 exists
to prevent.

**Decision:** each fact is `UNKNOWN` / `UP` / `DOWN`. `UNKNOWN` is not padding; it is what
makes the boot window honest. The policy is then a total function over 3 × 3 = 9 cells:

```
http == DOWN                  ->  RED, solid      "permanent — needs a reflash"
http == UP   && wifi == UP    ->  GREEN, solid    "reachable"
anything else                 ->  RED, blinking   "booting / connecting / reconnecting"
```

`http == DOWN` outranks everything, because it is permanent and unrecoverable in software.
That precedence is not invented here — it mirrors the rule already set at `src/main.c:107`:
*"LEVEL_FAULT is the more specific and more urgent condition, so it wins."*

One cell is worth naming because it looks wrong and is not: `http == UNKNOWN && wifi == UP`
is reachable, because `wifi_conn_start()` runs before `http_api_start()` in `app_main` and the
IP event can land first. It renders red-blinking, which is correct — there is an IP but no
server, so the dashboard genuinely is not reachable.

---

## Decision 4 — WS2812 driver: managed component or vendored encoder

`espressif/led_strip` is **not** bundled in ESP-IDF; the pinned framework tree carries only
`esp_driver_rmt` and `esp_driver_ledc`. So it would be a new registry dependency — in a
manifest that already carries a scar: `espressif/ds18b20` is pinned to 0.3.1 because 0.4.0's
`$CONFIG{}` conditional crashes the idf-component-manager vendored inside `espidf@6.9.0`.

IDF does ship `examples/peripherals/rmt/led_strip/main/led_strip_encoder.{c,h}` — 124 + 36
lines, Apache-2.0 with SPDX headers, a self-contained WS2812 RMT encoder with zero
dependencies.

**Decision: vendor the encoder.** We drive **one** LED with **three** colours. The managed
component's value is handling strip lengths, colour orders, and backends we will never use,
paid for with a registry dependency in a build that has already failed once on exactly that.
Provenance and the Apache-2.0 attribution go in `techContext.md`.

A third option — `esp_timer` instead of a task — was considered and rejected under the same
lens as Decision 5: it trades testable arithmetic for untestable lifecycle.

---

## Decision 5 — Module boundaries (Pure-Logic / Device-Only Split, 4th instance)

The project's `testability-patterns` learned rule is explicit: *"Apply this before writing
code, not after. Retrofitting the split means rewriting the module and its tests; a design
review is the cheap place to catch it."* So the split was drawn during the conversation.

| Unit | Kind | Owns |
|---|---|---|
| `lib/status_led_core/` | **pure** — no ESP/FreeRTOS/RMT includes, needs no `esp_shim.h` | fact→state derivation, blink phase, brightness scaling |
| `lib/status_led/` | device-only, thin | RMT TX channel + vendored WS2812 encoder |
| `lib/device_status/` | device-only, extended | two atomic facts, reporters, snapshot |
| `src/status_led_task.c` | device-only wiring | 100 ms tick: snapshot → core → show |

Dependency direction is strictly **device → pure**. `status_led_core.h` defines the fact types
itself rather than importing them from `device_status`, so the core depends on nothing at all —
which is what keeps it host-compilable with no shim. This also avoids the cost recorded in
`systemPatterns.md` § Testing Patterns, where `lib/sensor_hub` ended up depending on symbols
defined in `src/` and therefore cannot link standalone.

Everything with defect risk — the 9-cell table, blink phase across a `uint32_t` wrap,
brightness scaling overflow — lands in the pure half and is exhaustively host-tested. The
device half is acquire-and-delegate, matching the "wrapper must be genuinely thin" rule.

**Concurrency:** two `atomic_int`, no mutex. Each fact has exactly one writer and there is no
read-modify-write anywhere, so a mutex would add a potentially-blocking acquire **inside a
Wi-Fi event handler** and buy nothing. `reading_store` uses a mutex because it guards a
2,880-entry buffer; this guards two enums. The asymmetry is deliberate, not an inconsistency.

---

## Decision 6 — Kconfig `choice`, not `int` + `range`

Open deviation **D5** is `range 1 48` on four pin options, where the `comment` block warns in
prose and the `range` enforces nothing. A new pin option is a chance not to repeat it — but a
`range` cannot express this SoC's legal set, which is disjoint: 1–21 and 38–48, with 22–25
nonexistent (`SOC_GPIO_VALID_GPIO_MASK`), 26–32 SPI flash, and 33–37 octal PSRAM on the N16R8
part. Any single contiguous range is either too wide or too narrow.

**Decision: a `choice` of {48 (default), 47, 38}.** This makes an invalid pin
**unrepresentable** rather than merely discouraged — strictly stronger than what D5's `range`
attempts — and it encodes the 47-vs-48 documentation conflict in the menu itself, so the next
reader sees the ambiguity instead of rediscovering it. The three options are exhaustive for an
*onboard* LED; an external indicator on an arbitrary pin would be a different feature, and
adding an option then is cheaper than widening the range now.

Fixing D5 for the four existing pin options is explicitly **not** in scope — it is its own
task, and quietly absorbing it here would hide it from the work queue.

---

## Decision 7 — Details that are load-bearing rather than incidental

Recorded because each looks like a tunable constant and is not.

- **LED task priority 2.** The ESP-IDF main task runs at priority **1**, so priority 2
  preempts `app_main`. That is the only reason the LED can blink through the blocking boot
  sensor read, which is 1.1 s nominal and ~3.3 s when both I2C transactions hit their 1000 ms
  timeouts. At priority 1 the LED would be starved during exactly the window it is most
  wanted. Sampler stays above at 5.
- **`status_led_start()` early in `app_main`** — after the heap log, *before*
  `sampler_sensors_init()`, for the same reason.
- **`mem_block_symbols = 48`, not the example's 64.** Verified against the pinned IDF 5.3.1
  driver source rather than inferred:
  - `esp_driver_rmt/src/rmt_tx.c:247-248` validates that `mem_block_symbols` is **even and
    ≥ `SOC_RMT_MEM_WORDS_PER_CHANNEL`**, which is 48 on ESP32-S3 (`soc_caps.h:269`). So 48 is
    the legal minimum, not merely a preference — and it is even, so it passes.
  - `esp_driver_rmt/src/rmt_tx.c:115-117` computes
    `mem_block_num = mem_block_symbols / 48` and rounds **up** when the division is not exact.
    That is precisely why the example's 64 consumes **two** memory blocks and 48 consumes one.

  The example is sized for a 24-LED strip; we drive one LED, and 24 bits plus a reset code fits
  one block. This matters specifically because the 1-Wire bus already holds RMT channels, and
  Phase 2 of the previous feature already paid for an orphaned-channel bug. Final budget:
  **2 of 4 TX, 1 of 4 RX** (`SOC_RMT_TX_CANDIDATES_PER_GROUP` /
  `SOC_RMT_RX_CANDIDATES_PER_GROUP` = 4, `soc_caps.h:266-267`).
- **Blink period has 100 ms granularity, by construction.** `blink_ticks` is
  `HYDRO_STATUS_LED_BLINK_MS / 100`, integer division, so `range 100 2000` silently rounds any
  non-multiple down (150 ms → 1 tick → 100 ms actual). This is acceptable for a blink but must
  be stated in the option's `help` text rather than discovered — the alternative (validating a
  multiple in Kconfig) is not expressible.
- **Transmit only on change.** Solid green sends zero frames after the first; blinking sends
  2/sec instead of 10/sec. Same appearance, and it keeps the RMT peripheral quiet next to a
  timing-sensitive 1-Wire bus.
- **Brightness default 32/255 (~12%).** WS2812s are genuinely painful at full scale, and this
  one lives somewhere the user looks at.

---

## Decision 8 — Phasing, and why Phase 2 exists

Three phases, each with an observable deliverable:

1. **Pure policy core + ~20 host tests.** No hardware. No firmware behaviour change.
2. **Two-fact plumbing, observable on serial.** No LED.
3. **Driver, task, Kconfig, bench.**

Phase 2 is not padding — it is the de-risking step. If a fact is wired to the wrong event, the
failure is a plain-text log line rather than an ambiguous blink pattern in which the bug could
equally be the policy, the plumbing, the GPIO, or the byte order. It also means only Phase 3
needs the board, and only Phase 3 can break the build.

**Where Phase 2's log line comes from** (surfaced by the plan critique — it was underspecified).
Phase 2 needs a serial-observable derived state, but `src/status_led_task.c` does not exist until
Phase 3, so there must be a named owner for the logging. **Decision: the `report_wifi()` /
`report_http()` writers in `device_status` derive the state via
`status_led_core_state()` and log it on *change only*.** Consequences, both accepted:

- It creates a `lib/device_status` → `lib/status_led_core` dependency (device → pure, the correct
  direction). `device_status` can no longer build without the core. Stated here so the builder
  does not "solve" it by putting the derivation in `src/`, which would make it Phase-3-only and
  leave Phase 2 unobservable.
- The log is permanent, not scaffolding. It stays useful after Phase 3 — the serial console
  remains the only place a *dark* LED can be diagnosed, which is exactly the gap named in
  § Consequences accepted.
- Logging on change only (not per report) keeps a Wi-Fi reconnect storm from flooding the
  console, mirroring the same decision made for `status_led_show()` failures.

One nuance recorded against the `build-verification` learned rule, which warns that a new
`lib/` module with no call site can archive cleanly without reaching the linked image: in
Phase 1 that is **expected and correct**. The core's only caller is the test binary, and it
legitimately should not appear in the firmware image. The flash-delta check applies at Phase 3.

---

## Consequences accepted

| Consequence | Why accepted |
|---|---|
| The LED reads green while both sensors are offline | Decision 1 — user's explicit, informed choice |
| The LED cannot report its own failure | Inherent. Mitigated by never giving "dark" a meaning: dark = powered off or LED broken, both resolve to "read the serial console" |
| ~100 vendored lines we now own | Decision 4 — cheaper than a registry dependency in a build already burned by one |
| An always-on task at 10 Hz | Bounded: 2560 B stack, priority 2, transmit-on-change, ~+3 KB RAM (32.6% → ~33%) |
| `device_status` carries two parallel notions (`status_set()`'s four states and the two new facts) | Unifying them is a refactor of code Phases 1–5 depend on; deferred deliberately rather than smuggled into this task |
| GPIO 48 rests on vendor Q&A, not a schematic | Kconfig `choice` makes the fallback a menuconfig change, and the bench step settles it in a minute |

---

## Open bench-verification items

Carried into Phase 3 as acceptance criteria rather than assumptions:

1. GPIO 48 is the LED (fallback: 47, then 38, via `menuconfig`).
2. Byte order is GRB — a board wired RGB shows red for "reachable", which is self-revealing.
3. First blink appears < 1 s after power-on, *before* the boot sensor read completes. This is
   the behavioural check that the priority-2 preemption in Decision 7 actually works.
4. Solid red renders correctly, verified by an **injected fault** — forcing
   `http_api_start()` to return `ESP_FAIL` in a scratch build. Without it the `RED_SOLID`
   branch would ship having never been seen on hardware; the host tests prove the logic, not
   the rendering.
5. 32/255 brightness is readable but not glaring at night in the grow area.
6. **The DS18B20 still reads valid temperatures while the LED is blinking.** Raised by the plan
   critique: Decision 7's priority-2 choice makes the LED task *preempt* `app_main`, and both the
   LED and the 1-Wire bus are RMT consumers — so the LED task can now interrupt a 1-Wire
   transaction that the previous design could not. Assessed risk is **low**: RMT waveforms are
   generated by per-channel hardware rather than by the CPU, the channels are independent, and
   transmit-on-change means a blink costs roughly 80 µs twice a second. But "low" is an argument,
   not evidence, and the previous feature already lost a sample series to an unnoticed RMT
   interaction. Confirming a valid `water temperature` line on serial with the LED actively
   blinking costs one glance and converts the argument into a check.

**Perceived-brightness caveat** (noted, not actioned): red and green at the same numeric scale
are not equally bright to the eye — green LEDs read considerably brighter. If 32/255 turns out
well-matched for one colour and not the other, a per-colour scale is a one-line change in the
pure core's brightness function, and it is fully host-testable there. Not worth pre-solving.
