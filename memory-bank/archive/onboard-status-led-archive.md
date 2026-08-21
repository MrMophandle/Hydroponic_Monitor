# Archive: Onboard Status LED

## Metadata
- Task: `onboard-status-led`
- Complexity: Level 3
- Started: 2026-08-20
- Completed: 2026-08-21
- Roadmap Link: `onboard-status-led` (version `next`)
- Branch: `feature/onboard-status-led`
- Commits: `1574f85` (brainstorm) → `8d4e7fc` (Phase 1) → `b01f2e7` (Phase 2) → `0fefde5` (Phase 3) → `8816282` (reflection)

> **✅ BENCH VERIFICATION — every acceptance criterion is now MET, 2026-08-21.**
> Three bench sessions on the physical ESP32-S3 closed all 13 ACs: **GPIO 48 is the correct
> pin**, the **GRB byte order is correct**, all three presentation states render as designed,
> the `http == DOWN` precedence rule is confirmed empirically, and both Wi-Fi transition
> directions land in ≤20 ms against a 2 s budget. **One non-AC integration check remains**:
> DS18B20 RMT coexistence, blocked until the temperature probe is physically installed.

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
| AC-VERIFY-6 — GPIO from a Kconfig `choice`, no pin literal in source | MUST | ✅ **Met** | Source/Kconfig by code review; **GPIO 48 and GRB byte order confirmed on hardware 2026-08-21** |
| AC-VERIFY-7 — RMT channel budget + One-Owner-Per-Peripheral hold | MUST | ✅ Met | Code review; budget stays 1/4 TX, 1/4 RX |
| AC-VERIFY-8 — `HYDRO_STATUS_LED_ENABLE=n` disables cleanly, no partial acquisition | SHOULD | ✅ Met | Code review of the disable path |
| AC-ASYNC-1 — first illumination < 1 s after power-on, before the boot sensor read | MUST | ✅ **Met** | Call ordering in `src/main.c`; **illumination at boot confirmed on hardware 2026-08-21** (qualitative, not stopwatch-measured) |
| AC-ASYNC-2 — Wi-Fi drop and recovery each reflected within 2 s | MUST | ✅ **Met** | **Both directions confirmed on hardware 2026-08-21** via forced `esp_wifi_disconnect()`. Drop→LED **20 ms**; recovery→LED **<10 ms**. Reproduced twice |
| AC-ERROR-1 — LED init/task-creation failure logs and lets boot continue | MUST | ✅ Met | Code review of error paths |
| AC-ERROR-2 — repeating `status_led_show()` failure logs once, not per tick | MUST | ✅ Met | Code review of suppressed-count logging |
| AC-INTEGRATION-1 — RED_SOLID (HTTP-down) branch confirmed against real hardware | MUST | ✅ **Met** | **Fault injection on hardware 2026-08-21** — `max_uri_handlers = 0`; `RED_SOLID` visually confirmed and held through Wi-Fi association |

**All 13 acceptance criteria are MET.** Host-verifiable criteria by test; hardware-gated
criteria by three bench sessions on 2026-08-21. The only outstanding item is the DS18B20
RMT-coexistence integration check, which is not itself an AC — see below.

---

## Bench Verification

**All acceptance criteria MET — 2026-08-21, across three bench sessions.** 5 of 6 bench items
closed; the 6th (DS18B20 RMT coexistence) is blocked on hardware not yet installed and is an
integration check rather than an AC.

### Session 1 — normal boot, unmodified firmware

Reported observation: *"LED worked like a charm on boot. Flashed red, went to green when
Wi-Fi was connected."*

1. **AC-VERIFY-6 (hardware half) ✅** — **GPIO 48 is the correct pin.** The vendor Q&A answer was right and the vendor's own documentation (GPIO 47) was wrong.
2. **GRB byte order is correct ✅** — red rendered as red and green as green. A byte-order error would have swapped the two channels and made the "reachable" state red; it didn't.
3. **AC-ASYNC-1 ✅** — illumination present at boot, before the blocking sensor read. This confirms the load-bearing `status_led_start()`-before-`sampler_sensors_init()` ordering in `src/main.c` actually does what it was written to do. Observed qualitatively; not stopwatch-measured against the 1 s threshold.
4. **AC-ASYNC-2, forward direction ✅** — Wi-Fi association drove `RED_BLINK` → `GREEN_SOLID`.

The design intent held on first contact with hardware: the tri-state fact model meant the
boot window read as "still trying" (red blinking) rather than "needs a reflash" (red solid),
which is precisely the failure a boolean model would have produced.

### Session 2 — AC-INTEGRATION-1 fault injection ✅ CLOSED

**Method**: `config.max_uri_handlers = 0` added temporarily to `http_api_start()`, built,
flashed, observed, reverted. The reverted build came back to **965,632 B — byte-for-byte the
archived baseline**, confirming a clean revert with nothing left behind.

**`RED_SOLID` visually confirmed by the operator.** Serial evidence:

```
E (1955) http_api: httpd_start failed: ESP_ERR_HTTPD_ALLOC_MEM
E (1965) main: HTTP API FAILED to start (ESP_ERR_HTTPD_ALLOC_MEM) — dashboard is unreachable
I (1975) device_status: led state -> 2 (wifi=0 http=2)     # RED_SOLID
```

Wi-Fi then associated and acquired `172.30.100.220` at t≈3.0 s, and **no further state change
was logged** — the LED held `RED_SOLID`.

**The precedence rule is now confirmed empirically, not by inference.** The clean-firmware
reflash logged `led state -> 0 (wifi=1 http=1)` at t≈3.0 s, proving the Wi-Fi fact does flip
to `UP` on IP acquisition. Therefore during the injection run both `wifi == UP` and
`http == DOWN` held simultaneously and the derived state still stayed `RED_SOLID` —
`http == DOWN` genuinely outranks `wifi == UP` on real silicon, exactly as
`status_led_core.h:64` specifies.

**Injection path differed from the prediction.** The failure surfaced at `httpd_start()` with
`ESP_ERR_HTTPD_ALLOC_MEM` (`httpd_create` could not allocate a zero-length handler array),
*not* at `httpd_register_uri_handler()` with `ESP_ERR_HTTPD_HANDLERS_FULL` as expected. Same
observable outcome via a cleaner path — the server was never created at all.

### Session 3 — AC-ASYNC-2 reverse-direction injection ✅ CLOSED

**Method**: a temporary periodic `esp_timer` in `app_main()` calling `esp_wifi_disconnect()`
every 20 s, so the fact transition runs through `wifi_conn.c:142`'s real
`WIFI_EVENT_STA_DISCONNECTED` handler. Reverted after; the rebuild returned to 965,632 B,
byte-for-byte the baseline.

Operator confirmed: *"Green for ~18 s, a ~2 s red-blink burst, back to green — twice."*
Serial evidence, reproduced identically across two cycles:

```
W (21965) main: BENCH INJECTION: forcing esp_wifi_disconnect()
W (21985) wifi_conn: disconnected — reconnecting in 1 s
I (21985) device_status: led state -> 1 (wifi=2 http=1)     # RED_BLINK, wifi=DOWN
I (24115) device_status: led state -> 0 (wifi=1 http=1)     # GREEN_SOLID, wifi=UP
```

| Transition | Latency |
|---|---|
| Disconnect event → LED `RED_BLINK` | **20 ms** (both cycles) |
| `GOT_IP` event → LED `GREEN_SOLID` | **<10 ms** (state change shares a timestamp with `wifi_conn: connected`) |

Both directions are three orders of magnitude inside the 2 s budget. **`wifi=2` is
`STATUS_FACT_DOWN`**, confirming the fact genuinely traversed `UP → DOWN` through the
disconnect handler — the code path a board reset never exercises.

**Measurement caveat, stated so the record is not misread**: the wall-clock gap from
disconnect to green was 2130 ms / 2070 ms. That is *network* reconnect time — the 1 s backoff
floor plus ~1.1 s association and DHCP — **not** LED latency. The LED cannot legitimately turn
green before the network is actually back. AC-ASYNC-2 measures how fast the LED reflects an
event, which is the 20 ms / <10 ms above.

**Incidental confirmation**: both cycles logged `reconnecting in 1 s` rather than 1 s then
2 s, showing `wifi_backoff_reset()` correctly returns the sequence to its floor after each
successful recovery instead of ratcheting up. Not a target of this test.

### Outstanding ⬜

1. **RMT coexistence** — DS18B20 1-Wire reads succeeding while the LED blinks. **Blocked
   until the temperature probe is physically installed.** The DS18B20 driver acquires an RMT
   TX+RX pair and the LED holds one RMT TX, for 2 of 4 TX and 1 of 4 RX. Budget is fine on
   paper; this is the **only** remaining item, and the only one with real risk in it.

### Design limitation surfaced during bench testing (not a defect)

The WS2812 **latches its last received frame** and holds it until sent a new one or power is
removed — an ESP32 reset does not clear it. Discovered when a diagnostic build that never
calls `status_led_show()` left the LED sitting on green from the previous production run.

This exposes a case absent from the design's status vocabulary. The task file records that
*"dark means powered off or LED failed — read the serial console"*, but **if the tick task
dies while the board stays powered, the LED freezes on its last frame and looks perfectly
healthy.** A frozen green is visually indistinguishable from a live green, and unlike "dark"
there is no cue at all that the indicator has stopped updating.

Not a defect in what shipped — nothing kills the tick task in practice, and the LED is
explicitly diagnostic rather than safety-critical. But the indicator cannot report its own
death, which is worth knowing before anyone treats a green LED as proof of liveness. A
heartbeat (e.g. an imperceptible periodic re-transmit, or a slow breathing modulation on the
solid states) would close it if that assurance is ever wanted.

### Follow-up defect found during bench testing (not a blocker)

On the `httpd_register_uri_handler()` failure branch, `http_api_start()` returns the error
without calling `httpd_stop()`, leaking a running httpd task with a partially-registered
handler set. Same class as the RMT-leak-on-error-path finding the Phase 3 code review caught
and fixed. Not exercised by the injection above (which failed earlier, at `httpd_start()`).
Worth a Level 1 task.

**Process note:** this is the second consecutive feature (after `sensor-monitoring-dashboard`)
to *close* with undemonstrated hardware-gated criteria — though unlike the first, this one was
bench-verified within a day and the record updated rather than left stale. The reflection's
High Priority recommendation stands: the workflow needs a first-class bench-verification state
rather than prose in a task file's notes.

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

1. **Finish the bench procedure** — three items left, listed in § Bench Verification § Outstanding. The DS18B20 RMT-coexistence check unblocks as soon as the temperature probe is installed.
2. ~~**If GPIO 48 turns out to be wrong**, the Kconfig `choice` already offers 47 and 38.~~ **Resolved 2026-08-21** — GPIO 48 confirmed on hardware. The `choice` can stay for documentation value; the alternates are no longer expected to be needed.
3. **Ecosystem (High Priority, out of scope here)**: BMB needs a first-class "bench verification pending" status. Two consecutive features have now closed with undemonstrated hardware-gated criteria, represented only as prose. A third recurrence should not be needed to act on this.
4. **Track the brainstorm-compressed path** across the next several Level 3 features to see whether the plan-critique finding rate holds up against the separate `/bmb:plan` + `/bmb:creative` path.
