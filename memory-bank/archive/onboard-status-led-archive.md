# Archive: Onboard Status LED

## Metadata
- Task: `onboard-status-led`
- Complexity: Level 3
- Started: 2026-08-20
- Completed: 2026-08-21
- Roadmap Link: `onboard-status-led` (version `next`)
- Branch: `feature/onboard-status-led`
- Commits: `1574f85` (brainstorm) → `8d4e7fc` (Phase 1) → `b01f2e7` (Phase 2) → `0fefde5` (Phase 3) → `8816282` (reflection)

> **⚠️ ARCHIVED WITH PENDING BENCH VERIFICATION.** See § Bench Verification Pending.
> The code is complete, reviewed, and build-verified, but a subset of MUST-priority
> acceptance criteria are **implemented-but-undemonstrated** because they require a
> human with the physical ESP32-S3 board. This archive deliberately records them as
> pending rather than closed, per the reflection's explicit recommendation.

---

## Summary

Drives the ESP32-S3-N16R8's onboard addressable RGB LED (WS2812, GPIO 48) as a firmware
**reachability** indicator, so the device reports whether its dashboard is reachable with
no browser and no serial console attached.

Three presentation states, derived from exactly two facts:

| LED | Meaning |
|---|---|
| green, solid | Wi-Fi associated **and** HTTP server running — dashboard reachable |
| red, blinking | booting, connecting, or reconnecting — not reachable, still trying |
| red, solid | HTTP server never started — permanent, needs a reflash |

Sensor health is deliberately **not** shown. The LED reads green with both probes
unplugged; that is an accepted, recorded trade, not a defect to be "fixed" later.

The larger half of the work was not the LED but the **runtime status model**, which did
not previously exist. Before this feature, `status_set()` was called only from
`app_main()` at boot with no memory, no getter, and no way to represent a conjunction of
two independent facts; `wifi_conn.h` exported no connection-state getter; and
`http_api_start()` discarded its `httpd_handle_t`. This feature closes a seam that
`lib/device_status/` was built for during `sensor-monitoring-dashboard` Phase 2, whose
header had explicitly deferred the hardware consumer until the LED pin was confirmed.

---

## Requirements

### Original Requirements
- Report dashboard reachability on the onboard RGB LED without a browser or serial console
- Derive the LED state from exactly two independent facts (Wi-Fi associated, HTTP server running)
- Never let the LED lie during a normal boot (the boot window must not read as a permanent fault)
- Keep the LED GPIO configurable — the pin rests on vendor Q&A (GPIO 48), and the vendor's own docs disagree (GPIO 47)
- Keep LED policy host-testable; keep hardware-specific code thin and separate
- Fail open: no LED problem may prevent the firmware from booting
- Support a clean disable path, since the GPIO is unconfirmed

### Success Criteria
| AC | Priority | Status | How verified |
|---|---|---|---|
| AC-VERIFY-1 — green solid only when both facts UP | MUST | ✅ Met | Host test, exhaustive truth table |
| AC-VERIFY-2 — red blinking covers booting/connecting/reconnecting (5 named cells) | MUST | ✅ Met | Host test, exhaustive truth table |
| AC-VERIFY-3 — red solid iff HTTP fact is DOWN | MUST | ✅ Met | Host test, exhaustive truth table |
| AC-VERIFY-4 — 9-cell truth table exhaustively host-tested and registered | MUST | ✅ Met | 24 `status_led_core` tests |
| AC-VERIFY-5 — blink phase + brightness scaling tested at their edges | MUST | ✅ Met | Host tests at edge values |
| AC-VERIFY-6 — GPIO from a Kconfig `choice`, no pin literal in source | MUST | ⚠️ **Partial** | Source/Kconfig verified by code review; **physical GPIO + GRB byte order unconfirmed on hardware** |
| AC-VERIFY-7 — RMT channel budget + One-Owner-Per-Peripheral hold | MUST | ✅ Met | Code review; budget stays 1/4 TX, 1/4 RX |
| AC-VERIFY-8 — `HYDRO_STATUS_LED_ENABLE=n` disables cleanly, no partial acquisition | SHOULD | ✅ Met | Code review of the disable path |
| AC-ASYNC-1 — first illumination < 1 s after power-on, before the boot sensor read | MUST | ⚠️ **Partial** | Call ordering verified in `src/main.c`; **wall-clock timing not measured on hardware** |
| AC-ASYNC-2 — Wi-Fi drop and recovery each reflected within 2 s | MUST | ⚠️ **Partial** | 100 ms tick period implies it; **not observed on hardware** |
| AC-ERROR-1 — LED init/task-creation failure logs and lets boot continue | MUST | ✅ Met | Code review of error paths |
| AC-ERROR-2 — repeating `status_led_show()` failure logs once, not per tick | MUST | ✅ Met | Code review of suppressed-count logging |
| AC-INTEGRATION-1 — RED_SOLID (HTTP-down) branch confirmed against real hardware | MUST | ❌ **Not demonstrated** | Requires injected `http_api_start()` failure on the bench |

**Host-verifiable criteria: fully met. Hardware-gated criteria: see below.**

---

## Bench Verification Pending

The following require a human with the physical ESP32-S3 board. They are **implemented and
build-verified, not demonstrated**:

1. **AC-INTEGRATION-1** — inject an `http_api_start()` failure and confirm `RED_SOLID` appears
2. **AC-ASYNC-1 (bench half)** — confirm first illumination lands under 1 second from power-on
3. **AC-ASYNC-2 (bench half)** — observe a Wi-Fi drop and recovery reflected within 2 s each way
4. **AC-VERIFY-6 (hardware half)** — confirm the LED is on GPIO 48 (not the vendor-documented 47) and that the GRB byte order produces the intended colors, not a red/green swap
5. **RMT coexistence** — confirm DS18B20 1-Wire reads still succeed while the LED is actively blinking (both peripherals share the RMT block)
6. **Visual confirmation** of all three presentation states

Why this matters: the GPIO choice rests on **vendor Q&A that contradicts the vendor's own
documentation**. Until item 4 is done, "the LED works" is an inference from correct code,
not an observation. The Kconfig `choice` (48 / 47 / 38) and the `HYDRO_STATUS_LED_ENABLE=n`
escape hatch exist precisely because this was known to be unconfirmed.

**This is the second consecutive feature** (after `sensor-monitoring-dashboard`) to close
with undemonstrated hardware-gated criteria. The reflection raises this as a High Priority
ecosystem gap: the workflow needs a first-class "bench verification pending" state rather
than prose carried in a task file's notes.

---

## Implementation

### Approach

Three phases, each independently valuable and independently verifiable, built on the
project's established **Pure-Logic / Device-Only Split**:

- **Phase 1** — the pure policy core, with no hardware and no firmware behavior change at all. All the decision logic, all the tests.
- **Phase 2** — the two-fact plumbing, observable on the serial console with no LED involved.
- **Phase 3** — the WS2812 driver and tick task: the first phase that touches hardware, and the only one that could not be host-tested.

This ordering meant the *logic* was proven before any hardware dependency existed, so the
untestable phase was reduced to a thin, reviewable shim.

### Key Components

1. **`lib/status_led_core/`** — pure policy, no platform dependencies
   - Purpose: derive a presentation state from the two-fact pair; compute the RGB frame including blink phase and brightness scaling
   - Files: `include/status_led_core.h`, `src/status_led_core.c`
   - Exhaustively host-tested: all 9 cells of the `(wifi, http)` truth table plus edge values

2. **`lib/device_status/`** (extended) — the runtime fact store
   - Purpose: atomic reporting and snapshotting of the two reachability facts from Wi-Fi event handlers
   - Files: `include/device_status.h`, `src/device_status.c`
   - Made host-testable via the `ESP_PLATFORM`/`esp_shim.h` conditional-include pattern already proven by `lib/sensor_hub` — the **second instance** of that sub-pattern

3. **`lib/status_led/`** — device-only WS2812 driver
   - Purpose: one RMT TX channel; transmit a 3-byte GRB frame. Knows nothing about policy
   - Files: `include/status_led.h`, `src/status_led.c`, `include/led_strip_encoder.h`, `src/led_strip_encoder.c`
   - `led_strip_encoder.{h,c}` vendored **verbatim** from the ESP-IDF example (Apache-2.0), diff-confirmed byte-identical — chosen over adding a managed component

4. **`status_led_task`** — device-only FreeRTOS tick task
   - Purpose: 100 ms tick; read facts → derive state → compute frame → transmit on change only
   - Files: `include/status_led_task.h`, `src/status_led_task.c`
   - Priority 2 (one above FreeRTOS main) so the LED animates during boot, before blocking sensor reads
   - Suppressed-error-count logging (AC-ERROR-2): first failure logged, consecutive failures counted silently, recovery logged with the suppression count

5. **Kconfig menu** — `src/Kconfig.projbuild`
   - `HYDRO_STATUS_LED_ENABLE`, `HYDRO_STATUS_LED_GPIO` (`choice`: 48 / 47 / 38), `HYDRO_STATUS_LED_BRIGHTNESS` (0–255, default 128), `HYDRO_STATUS_LED_BLINK_MS`

### Design Decisions

- **Tri-state facts (`UNKNOWN` / `UP` / `DOWN`), not booleans** — the load-bearing decision of the whole feature. With booleans, an unstarted HTTP server is indistinguishable from a failed one, so the LED would claim "needs a reflash" during *every normal boot*. The tri-state model plus a precedence rule makes that failure mode structurally impossible.
- **Atomics over a mutex** for cross-task fact reporting — facts are written from Wi-Fi event handler context; the snapshot needs consistency, not mutual exclusion.
- **Vendoring the IDF RMT encoder** rather than adding a managed component — avoids a dependency for ~124 lines of stable example code; provenance recorded.
- **Kconfig `choice` rather than `range`** for the GPIO — deliberately avoids re-committing deviation D5 from a prior task, and encodes the fact that only three pins are plausible candidates.
- **Reachability only, sensor health excluded** — keeps the LED's vocabulary narrow enough to be unambiguous at a glance.

Reference: `memory-bank/creative/onboard-status-led-design.md`

---

## Testing

- **Unit tests added: 32 host tests** — 24 `status_led_core` (Phase 1) + 8 `device_status` (Phase 2)
- **Native suite: 63 → 71 tests**, all passing (`pio test -e native`)
  - Breakdown: 11 reading_store + 10 level_switches + 6 sensor_hub + 4 wifi_backoff + 6 reading_json + 2 reading_store time_valid + 24 status_led_core + 8 device_status
- **Integration tests: 0 added** — see below
- **All tests passing**: ✅

### Phase 3 added zero host tests — by design, not by omission

The RMT driver and FreeRTOS task cannot run under `[env:native]` (no RMT peripheral, no
scheduler on the host). Rather than write tests that would only exercise mocks, Phase 3
substituted a **regression gate**: the native suite must remain at 71/71, unchanged, before
and after. It did.

This is an honest trade, but it is the reason five acceptance criteria are only partially
verified. The reflection extracted a learning from it: pre-commit the "what NOT to test and
why" list during planning, so a zero-new-tests phase is a decision made in advance rather
than a justification offered afterward.

### Build verification
| Phase | RAM | Flash | Note |
|---|---|---|---|
| Phase 1 | 32.6% | 30.6% | pure library, not yet linked by any caller |
| Phase 2 | 32.6% (106,708 B) | 30.6% | additive only |
| Phase 3 | 32.6% (106,708 B) | **30.7% (965,632 B)** | first non-zero delta — the core's first real firmware caller |

That non-zero flash delta at Phase 3 is itself the evidence that the pure core actually
reached the linked image, per the `build-verification` learned rule.

### Lint
1 new LOW cppcheck `unusedFunction` false positive on `status_led_start` — the same class as
9 pre-existing false positives on cross-file-called functions; the function is genuinely
called at `src/main.c:74`. Accepted as documented baseline noise, not a regression.

### Code review
All three phases **APPROVED with 0 blocking findings** and zero commit-guard flags.
- Phase 2: 1 OPTIONAL finding applied (doc-comment noting the benign `s_last_logged_state` race — it can duplicate or miss a log line, never produce a wrong derived LED state)
- Phase 3: 1 RECOMMENDED finding applied (RMT channel and encoder were not released on `status_led_init()`'s `rmt_new_led_strip_encoder` / `rmt_enable` failure branches; cleanup added, build re-verified)
- Security: PASS on all phases — no user input, no network-parsing surface, 0 new external dependencies

---

## Files Changed

**New — pure logic (host-tested)**
- `lib/status_led_core/include/status_led_core.h` — policy API: state derivation, frame computation
- `lib/status_led_core/src/status_led_core.c` — the 9-cell truth table, blink phase, brightness scaling
- `test/test_status_led_core/test_status_led_core.c` — 24 tests, exhaustive over the truth table and edges
- `test/test_device_status/test_device_status.c` — 8 tests over the two-fact store

**New — device-only**
- `lib/status_led/include/status_led.h`, `src/status_led.c` — RMT TX channel, GRB frame transmit
- `lib/status_led/include/led_strip_encoder.h`, `src/led_strip_encoder.c` — vendored ESP-IDF encoder (Apache-2.0)
- `include/status_led_task.h`, `src/status_led_task.c` — 100 ms tick task, transmit-on-change, suppressed-error logging

**Extended**
- `lib/device_status/include/device_status.h`, `src/device_status.c` — reachability facts + host-testability shim
- `src/main.c` — early `status_led_start()` before `sampler_sensors_init()` (AC-ASYNC-1 ordering)
- `src/wifi_conn.c` — report the Wi-Fi fact from the event handlers
- `src/Kconfig.projbuild` — new "Status LED (onboard RGB)" menu
- `platformio.ini` — `status_led` added to `esp32-s3-devkitm-1` `lib_deps` only; native `test_filter` entries
- `sdkconfig.esp32-s3-devkitm-1` — generated config for the new Kconfig symbols

**Memory bank**
- `memory-bank/tasks/onboard-status-led.md`, `memory-bank/creative/onboard-status-led-design.md`, `memory-bank/reflection/onboard-status-led-reflection.md`, `memory-bank/roadmap/onboard-status-led.md`, `memory-bank/roadmap/versions/next.md`, `memory-bank/techContext.md`

---

## Lessons Learned

- **The tri-state fact model was the whole feature.** A boolean model would have shipped an LED that claims "needs a reflash" on every normal boot. This was caught at design time, before any code — which is the only cheap place to catch it.
- **Plan critique earned its keep.** It found that AC-ASYNC-1's "within 1 s" threshold was ambiguous between *entering the state* (< 100 ms) and *a lit→dark blink edge* — and that a perfectly legal `HYDRO_STATUS_LED_BLINK_MS=2000` would therefore fail a MUST-priority AC on correctly-built firmware. The AC was rewritten to assert *first illumination*. 8 critique findings total: 5 applied, 2 noted, 1 refuted.
- **`/bmb:brainstorm` compressed plan + creative without losing rigor** — the critique gate still caught a ship-blocker. One data point, not a trend; worth tracking across the next few Level 3 features before generalizing.
- **A zero-new-tests phase should be pre-committed, not post-justified.** Phase 3's exemption was correct, but it was argued at build time. Deciding it during planning would have made the resulting AC coverage gap visible earlier.
- **The `ESP_PLATFORM`/`esp_shim.h` pattern is now proven twice** (`sensor_hub`, then `device_status`) and should be the default reach for host-testing a device-touching module, rather than re-deriving an approach each time.

Reference: `memory-bank/reflection/onboard-status-led-reflection.md`

---

## References
- Task: `memory-bank/tasks/onboard-status-led.md`
- Creative: `memory-bank/creative/onboard-status-led-design.md`
- Reflection: `memory-bank/reflection/onboard-status-led-reflection.md`
- Roadmap: `memory-bank/roadmap/onboard-status-led.md`
- Timeline: `git log main..feature/onboard-status-led`

---

## Follow-up

1. **Run the 8-step bench procedure** documented in the task file and record the outcome — this closes AC-INTEGRATION-1, the bench halves of AC-ASYNC-1/2, and the hardware half of AC-VERIFY-6. Until then, confirm nothing about the LED from this archive alone.
2. **If GPIO 48 turns out to be wrong**, the Kconfig `choice` already offers 47 and 38 — no code change needed, which is why it was built that way.
3. **Ecosystem (High Priority, out of scope here)**: BMB needs a first-class "bench verification pending" status. Two consecutive features have now closed with undemonstrated hardware-gated criteria, represented only as prose. A third recurrence should not be needed to act on this.
4. **Track the brainstorm-compressed path** across the next several Level 3 features to see whether the plan-critique finding rate holds up against the separate `/bmb:plan` + `/bmb:creative` path.
