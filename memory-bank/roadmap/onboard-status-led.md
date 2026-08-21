---
version: next
status: completed
bench_verification: pending
priority: medium
complexity: 3
linked_tasks: [onboard-status-led]
created: 2026-08-20
completed: 2026-08-21
---

# Onboard Status LED

> **Status: completed — with bench verification pending.** All three phases are built,
> reviewed, and build-verified; the task `onboard-status-led` is COMPLETE and archived at
> `memory-bank/archive/onboard-status-led-archive.md`. A subset of MUST-priority acceptance
> criteria remain **implemented-but-undemonstrated** because they need a human at the bench —
> see § Bench Verification Pending below. This feature is closed for development, not for
> verification.

Drive the ESP32-S3-N16R8's onboard addressable RGB LED (WS2812 on GPIO 48) as a
firmware reachability indicator, so the device reports its own health without a
browser or a serial console attached.

The LED's vocabulary is deliberately narrow — **reachability only**:

| LED | Meaning |
|---|---|
| green, solid | Wi-Fi associated **and** the HTTP server is running — the dashboard is reachable |
| red, blinking | booting, connecting, or reconnecting — not reachable yet, still trying |
| red, solid | the HTTP server never started — permanent, needs a reflash |

Sensor health is explicitly **not** shown. The LED will read green with both
probes unplugged; that is an accepted trade, recorded so it is not "fixed" by
accident. Sensor state remains a dashboard and log concern.

This feature completes a seam the firmware already carries. `lib/device_status/`
was built in Phase 2 of `sensor-monitoring-dashboard` for exactly this purpose,
and its header records why the hardware consumer was deferred: *"the Hosyond
ESP32-S3-WROOM-1's onboard status LED availability/GPIO is unconfirmed … the
hardware consumer is deferred until that pin is confirmed on the bench."* The pin
is now identified as GPIO 48 (vendor Q&A; the vendor's own docs disagree, citing
GPIO 47), so the blocker is resolved rather than assumed — the pin stays a Kconfig
`choice` with a bench-confirmation step.

The larger half of the work is not the LED but the **runtime status model**, which
does not exist today: `status_set()` is called only from `app_main()` at boot and
has no memory, no getter, and no way to represent a conjunction of two independent
facts; `wifi_conn.h` exports no connection-state getter; and `http_api_start()`
discards its `httpd_handle_t`. The feature adds two-fact tracking with atomic
reporting from the Wi-Fi event handlers, a pure policy core deriving the
presentation state, and a thin WS2812 driver.

**Complexity rationale**: Level 3 by the decision tree, stopping at Q4 — the work
requires design decisions (the tri-state fact model and its precedence rule,
atomics-versus-mutex for cross-task fact reporting, vendoring the IDF RMT encoder
versus adding a managed component, a Kconfig `choice` rather than a `range` to
avoid re-committing deviation D5) and it affects multiple components (three new
`lib/` modules plus `device_status`, `main.c`, `wifi_conn.c`, `Kconfig.projbuild`,
and `platformio.ini`) without being system-wide. Q5 is not reached. Although the
work is delivered in three phases, nothing outside the status-indication path
changes behavior, no existing contract moves, and rollback risk is low because the
LED is diagnostic — nothing in the firmware depends on it. That containment is
what separates this from the Level 4 `sensor-monitoring-dashboard`, which was
effectively the entire firmware.

---

## Delivered

| Phase | Commit | Content |
|---|---|---|
| 1 | `8d4e7fc` | `lib/status_led_core/` — pure policy core, 24 exhaustive host tests |
| 2 | `b01f2e7` | `lib/device_status/` extended — two-fact reachability plumbing, 8 host tests |
| 3 | `0fefde5` | `lib/status_led/` RMT WS2812 driver, `status_led_task` tick task, Kconfig menu |

Native test suite 63 → 71. Device build RAM 32.6%, flash 30.6% → 30.7% (the delta at Phase 3
confirms the pure core reached the linked image). All three phases: code review APPROVED, 0
blocking findings, security PASS, 0 new external dependencies.

## Bench Verification Pending

Requires a human with the physical ESP32-S3 board. Not blocking the merge; blocking the
claim that the LED works.

1. **AC-INTEGRATION-1** — inject an `http_api_start()` failure, confirm `RED_SOLID`
2. **AC-ASYNC-1 (bench half)** — confirm first illumination under 1 s from power-on
3. **AC-ASYNC-2 (bench half)** — observe a Wi-Fi drop and recovery reflected within 2 s each way
4. **AC-VERIFY-6 (hardware half)** — confirm GPIO 48 (not the vendor-documented 47) and that GRB byte order yields the intended colors, not a red/green swap
5. **RMT coexistence** — confirm DS18B20 1-Wire reads still succeed while the LED is blinking
6. **Visual confirmation** of all three presentation states

Item 4 is the reason this section exists. As the feature description above notes, the pin
"stays a Kconfig `choice` with a bench-confirmation step" — that step has not happened, and
the GPIO rests on vendor Q&A that **contradicts the vendor's own documentation**. If 48 is
wrong, the `choice` already offers 47 and 38 and no code change is needed.
