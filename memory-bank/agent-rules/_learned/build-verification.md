---
name: Build Verification
globs: ["platformio.ini", "CMakeLists.txt", "*.cmake", "sdkconfig*", "**/idf_component.yml"]
paths: ["lib/", "src/"]
topics: ["build", "embedded", "linking", "toolchain", "verification"]
priority: low
auto_generated: true
derived_from: [sensor-monitoring-dashboard, onboard-status-led]
evidence_count: 2
last_validated: 2026-08-21
---

# Build Verification

A successful compile is evidence that the compiler was satisfied — not that the artifact
contains what you intended. In embedded builds these come apart routinely.

- Assert a new `lib/` module actually reaches the **linked image** whenever it has no call
  site in the same phase: check symbol presence (`nm firmware.elf | grep <symbol>`) or a
  non-trivial flash-size delta against the prior build. A clean cross-compile of an
  archived-but-unlinked static library is indistinguishable from a correctly linked one at
  the "build SUCCESS" level.
- Treat a flash/RAM figure **byte-identical to the previous phase** as a failure signal,
  not a stability signal, when that phase added code.
- Verify any first-time use of a documented-but-unexercised build mechanism (an embed
  macro, a link option, a code-generation step) with a **from-scratch clean rebuild**
  before trusting it — `rm -rf` the build dir, do not rely on an incremental build. Record
  the toolchain-pin/mechanism pairing that was actually verified, since the same mechanism
  may work on a different pin.
- Assume a documented mechanism can fail **silently** under a specific pinned version.
  Failure modes seen: a build-time "source not found" for a macro that never ran, and an
  "undefined reference" surfacing only at final link, far downstream of the real cause.
- Read build warnings on every phase. A warning that repeats every build is the easiest
  kind to stop seeing, and the most likely to be describing a real misconfiguration.
- The flash-delta check works in the positive direction too: when a phase finally gives a
  previously-unlinked pure library its first real caller, a **non-zero flash delta is the
  confirmation** that the library reached the image. Record which phase produced the first
  delta — a library that stayed byte-identical across the phase that was supposed to call it
  was never linked. (Confirmed on `onboard-status-led`: flash moved 30.6% → 30.7% only at
  Phase 3, the phase that added the caller.)
- A cppcheck `unusedFunction` warning on a function called from another translation unit is
  a known false positive in this build configuration. Count the pre-existing ones and treat
  only an increase *outside* that class as a regression — but re-confirm the call site by
  file:line each time rather than assuming the class membership.
