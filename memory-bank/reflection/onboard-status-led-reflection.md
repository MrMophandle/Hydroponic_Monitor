# Reflection: onboard-status-led - Onboard Status LED

**Date**: 2026-08-21
**Task Complexity**: Level 3
**Total Phases**: 3 (Pure policy core, Two-fact plumbing, WS2812 driver + task + Kconfig)
**Duration**: 2026-08-20 (brainstorm, roadmap feature + task + creative approved) to 2026-08-21 (all 3 build phases committed) — one calendar day of elapsed wall-clock activity across two sessions

## Executive Summary

This task closed a blocker `lib/device_status/` had carried since `sensor-monitoring-dashboard` Phase 2: its own header documented that the onboard LED consumer was deferred because the GPIO was unconfirmed on the bench. The user resolved the pin (GPIO 48, via vendor Q&A) mid-conversation, and the resulting `/bmb:brainstorm` session produced a fully-settled architecture in one dialogue — a tri-state (`UNKNOWN`/`UP`/`DOWN`) two-fact model, a 9-cell precedence table, atomics over a mutex, a vendored WS2812 encoder instead of a new registry dependency, and a Kconfig `choice` instead of another `int`+`range` deviation. Three phases followed the plan exactly: a pure policy core with 24 exhaustive host tests (Phase 1), device-only fact plumbing into `device_status` with 8 more tests (Phase 2, native suite 63→71), and the WS2812 driver/tick task/Kconfig menu with zero new tests by design (Phase 3, RMT/FreeRTOS cannot run under `[env:native]`). All three phases passed code review with zero blocking findings; the one RECOMMENDED finding (RMT resource cleanup on `status_led_init()` error paths) was applied and re-verified in-band; the commit guard passed on the first attempt in every phase, with zero re-invocations.

The implementation quality is high and the plan-critique process caught a real defect before it could ship: `AC-ASYNC-1`'s "first RED_BLINK within 1s" wording was ambiguous enough that a legal `BLINK_MS=2000` configuration would have failed a MUST-priority acceptance criterion on a correctly-built firmware — this was rewritten to assert first illumination rather than a blink edge. The tri-state fact model itself is the standout technical decision: a boolean model would have made the LED lie ("permanently broken, reflash me") for the first second of every boot, and the design doc names this precisely and traces it to the same precedence logic already established in `main.c`. The one honest gap, called out in the task file itself rather than glossed over, is that the feature is code-complete and build-verified only — the documented 8-step physical bench procedure (visual LED observation across all three states, an injected `http_api_start()` failure to confirm `RED_SOLID` on real hardware, GRB byte-order confirmation, and a DS18B20-reads-while-blinking RMT-coexistence check) has not been performed. That gap is structural, not a process failure: no build agent has a physical ESP32-S3 board, and the task file says so explicitly rather than claiming a false completion.

On the ecosystem side, this is a useful data point for `/bmb:brainstorm` as a compression of `/bmb:plan` + `/bmb:creative` into one dialogue for a task this shape: bounded scope, an existing well-understood seam (`lib/device_status`), and a single external unknown (the GPIO) that the user could resolve on the spot. The compression worked without any loss of rigor — the same plan-critique gate ran and caught the same class of finding it would have caught in the separate-commands path. But it leaves a visible seam-integrity question this reflection surfaces below: is a hardware-gated MUST acceptance criterion (AC-INTEGRATION-1) adequately represented by a task-file note, or does the workflow need a first-class "verification pending — requires human + hardware" status distinct from `BUILD_COMPLETE`?

---

## Plan vs Reality

- **Original estimate**: 3 phases (pure core → plumbing → driver/task/Kconfig), as scoped in the creative design's Decision 8 ("Phasing, and why Phase 2 exists")
- **Actual**: 3 phases, executed in the planned order, with no added or dropped phases
- **Deviations**: None at the phase-structure level. Within Phase 3, one code-review-driven addition (RMT channel/encoder cleanup on `status_led_init()` error paths) was applied directly by the orchestrator rather than deferred — a same-phase fix, not a scope change. The plan critique made 8 findings against the pre-build task file (5 applied, 2 noted for build-time attention, 1 refuted after checking the pinned IDF driver source) — all resolved before Phase 1 started, so none of them show up as in-flight plan deviations during the three build phases themselves.

## What Went Well

### Technical
- **The tri-state fact model was designed correctly the first time**, and is traceable to a specific failure mode the design doc names explicitly: a boolean `http` fact defaults to "down," so the `http == DOWN → RED_SOLID` rule would fire before `http_api_start()` had even run, making the LED falsely claim "needs a reflash" during every normal boot. `UNKNOWN` as a genuine third state, not padding, is the single decision this feature's correctness rests on, and AC-VERIFY-2's 5-cell blink coverage plus the Phase 2 host tests ("boot-window honesty: UNKNOWN/UNKNOWN must derive RED_BLINK, not RED_SOLID") both verify it directly.
- **The Pure-Logic/Device-Only Split was applied cleanly as its 4th documented instance** (per the creative doc), and Phase 2 made a second instance of a specific sub-pattern — the `ESP_PLATFORM`/`esp_shim.h` conditional-include trick already proven by `lib/sensor_hub` — to make `device_status.c` itself host-testable rather than leaving the new facts device-only. This is the kind of reuse the project's own `systemPatterns.md` exists to encourage, and it happened without prompting.
- **Two engineering claims in the design were verified against source rather than asserted from memory**: `mem_block_symbols = 48` (checked against the pinned IDF 5.3.1 `rmt_tx.c:247-248` and `:115-117` rather than copied from the vendored example's `64`), and the plan-critique's own re-check of that same claim (finding #5, "refuted" — the design was right, and the citation was upgraded from reasoned to verified rather than left as an assumption). This is a good instance of the workflow's built-in skepticism paying for itself.
- **Vendoring the WS2812 encoder instead of adding `espressif/led_strip` was the right call for the stated reason.** The manifest already carries a scar (`espressif/ds18b20` pinned to `0.3.1` because `0.4.0` crashes the vendored `idf-component-manager`), and the vendored encoder is 160 lines with an intact Apache-2.0 SPDX header, diff-confirmed byte-identical to the upstream example. This is a defensible, evidence-based trade — not just "avoid dependencies on principle."
- **Phase 2 existing purely as a de-risking step paid off as designed.** Its stated purpose — a serial-observable log line so a wiring bug shows up as plain text rather than an ambiguous blink pattern that could be policy, plumbing, GPIO, or byte-order — is exactly the kind of phase boundary that earns its keep only if Phase 3 goes smoothly, which it did (zero guard flags, one code-review finding, applied in-band).

### Process
- **The commit guard and code review passed clean on all three phases, with zero re-invocations** — see the Guardrail Misses section below for what that means and doesn't mean.
- **The plan critique caught a real, ship-blocking ambiguity before any code was written.** `AC-ASYNC-1`'s original wording ("first RED_BLINK transition within 1s") was genuinely ambiguous between "enters the state" (<100ms) and "a lit→dark blink edge" (`BLINK_MS`-dependent, default 500ms but Kconfig-legal up to 2000ms). A legal, correctly-built firmware with `BLINK_MS=2000` would have failed a MUST acceptance criterion under the edge reading. This was caught and rewritten before Phase 3 touched the timing-sensitive code, not discovered at the bench.
- **Deliberate scope exclusion was recorded, not just decided.** "The LED reads green with both sensors unplugged" is the LED-analogue of the `dashboard-empty-state-a11y` blank-chart bug fixed the same week, and the creative doc names that parallel explicitly and states the trade was shown to the user before they chose it. This is the right way to record a decision a future reader will be tempted to "fix" without realizing it was intentional.

## Challenges Encountered

### Hardware-gated acceptance criteria cannot be verified by a build agent
- **Description**: AC-INTEGRATION-1 (RED_SOLID confirmed on real hardware via an injected `http_api_start()` fault) and several bench-verification items (GPIO/byte-order confirmation, first-blink timing, DS18B20-while-blinking RMT coexistence) are written as MUST-priority acceptance criteria in the task file, but no build agent has a physical ESP32-S3-N16R8 board. The task correctly stops at "code-complete and build-verified" and documents the remaining bench procedure as a one-time human action.
- **Resolution**: The task file's Resumption Notes are explicit and honest about this: `BUILD_COMPLETE` is recorded, but the notes name the exact next human action (flash the board, run the 8-step bench procedure) before `/bmb:reflect` and `/bmb:archive` proceed. Nothing was silently marked "done."
- **Prevention**: This is not really preventable — hardware-in-the-loop is a genuine constraint on this project. But see the ecosystem section below: the *representation* of this state (a MUST AC sitting inside a task marked `BUILD_COMPLETE`, distinguishable only by reading prose in the Resumption Notes) is a workflow gap worth naming, not a task-execution failure.

### Phase 3's "zero new tests" decision needed to be stated, not just true
- **Description**: Phase 3 is entirely RMT peripheral code and a FreeRTOS task — neither compiles under `[env:native]`, so the honest test strategy is zero new host tests, substituting a regression gate (71/71 unchanged before and after) for RED→GREEN.
- **Resolution**: This was pre-committed in the Test Strategy section's "What NOT to Test" list before Phase 3 started (RMT transactions, WS2812 bit timing, byte order, FreeRTOS scheduling — all explicitly device-only, bench-verified) rather than discovered as an excuse during the build. The regression gate is a real check, not a rubber stamp: it would have caught any Phase 3 change that broke Phase 1/2 behavior.
- **Prevention**: N/A — this was handled correctly from the plan stage forward. Worth naming as a positive pattern: deciding and documenting "what NOT to test" during planning, before the phase that needs the exemption, kept this from reading as a shortcut taken under pressure.

## Creative Decision Assessment

### Decision 3 — Tri-state fact model
- **Decision**: Facts (`wifi`, `http`) are `UNKNOWN`/`UP`/`DOWN`, not boolean.
- **Outcome**: Directly host-tested in Phase 2 ("boot-window honesty" test case) and structurally load-bearing for AC-VERIFY-2's 5-cell RED_BLINK coverage. No defect surfaced against this decision in any phase.
- **Verdict**: Good — this is the correct decision and it held up completely.

### Decision 4 — Vendor the WS2812 encoder rather than add a managed component
- **Decision**: Copy `led_strip_encoder.{c,h}` verbatim from the ESP-IDF example (Apache-2.0) instead of adding `espressif/led_strip` as a registry dependency.
- **Outcome**: Diff-confirmed byte-identical in Phase 3's TDD step; zero new external dependencies per the code-reviewer's security pass. Provenance recorded in `techContext.md`.
- **Verdict**: Good — the manifest's existing `espressif/ds18b20` pin-down scar made this the evidently lower-risk choice, and it was reasoned from that specific prior incident rather than a general dependency-aversion instinct.

### Decision 6 — Kconfig `choice` instead of `int` + `range`
- **Decision**: `HYDRO_STATUS_LED_GPIO` is a 3-way `choice` (48/47/38), making an invalid pin unrepresentable, rather than repeating deviation D5's `range 1 48` pattern.
- **Outcome**: AC-VERIFY-6 verified this directly (grep for pin literals — none found outside the Kconfig-derived macro). This is strictly stronger than D5's approach and was explicitly scoped to not also retrofit D5's four existing pin options, which stays a separate task rather than being smuggled in here.
- **Verdict**: Good — and notable as a case where a known project deviation (D5) was learned from and not repeated, without over-scoping into fixing the original deviation.

### Decision 7 — LED task priority 2 (preempts app_main)
- **Decision**: Priority 2, one above the ESP-IDF main task's priority 1, so the LED can animate through the 1.1–3.3s blocking boot sensor read.
- **Outcome**: This decision's second-order risk (the LED task and the 1-Wire bus are now both RMT consumers, and priority 2 means the LED can now interrupt a 1-Wire transaction) was caught by the plan critique (finding #4) and mitigated with a specific 8th bench-verification step, not by re-litigating the priority choice itself. The risk assessment (RMT waveforms are hardware-generated per-channel, independent channels, ~80µs twice a second) is reasoned but explicitly labeled "an argument, not evidence" in the design doc — and the corresponding bench check to convert it into evidence has not yet been run.
- **Verdict**: Good design, unconfirmed in practice — this is precisely the open bench-verification gap. The decision is well-reasoned and the risk-mitigation step (checking DS18B20 reads continue while the LED blinks) is exactly the right check; it simply has not been executed yet because it requires the physical board.

---

## Guardrail Misses & Root-Cause Analysis

**Zero guard flags and zero sub-agent re-invocations occurred across all three phases.** The task file's `### Guard & Recovery Log` reads "(none — commit guard passed on first run)" identically in the Phase 1, Phase 2, and Phase 3 execution states (confirmed via `git show 8d4e7fc:...`, `git show b01f2e7:...`, and the current tip). Code review returned APPROVED with 0 blocking findings in every phase; the only findings were 1 optional (Phase 2, a doc-comment on a benign race, applied) and 1 recommended (Phase 3, RMT cleanup on error paths, applied directly by the orchestrator without requiring a re-dispatch). This is a clean build, and a clean build is itself a signal worth recording: it suggests the plan-critique and creative-design phases genuinely front-loaded the hard decisions (tri-state facts, atomics vs. mutex, Kconfig `choice`, vendored encoder, task priority) rather than leaving them to be discovered mid-implementation.

No Memory-Bank Corrections are warranted from this build — there is no stale or incorrect guidance to trace a miss back to, because there was no miss.

---

## Lessons Learned

### Technical
- The `ESP_PLATFORM`/`esp_shim.h` conditional-include pattern is now proven on two independent modules (`lib/sensor_hub`, then `lib/device_status`) as the project's standard mechanism for making a device-touching module host-testable. It should be treated as the default first option, not a case-by-case rediscovery, whenever a device-only module needs even partial host-test coverage.
- Deciding "what NOT to test" during planning — not during the phase that needs the exemption — kept Phase 3's zero-new-tests decision from reading as a shortcut. This is a generalizable practice: a hardware-only phase's test exemption should be written into the Test Strategy before any phase touches hardware, not justified after the fact.

### Process
- Compressing `/bmb:plan` + `/bmb:creative` into a single `/bmb:brainstorm` dialogue did not weaken the plan-critique gate — the same class of finding (the AC-ASYNC-1 ambiguity) was caught at the same point in the pipeline. For a Level 3 task with a bounded, well-understood seam and a single external unknown the user could resolve live, this compression appears to cost nothing in rigor while saving a command round-trip.
- A task file's `BUILD_COMPLETE` status and its Resumption Notes currently carry the entire signal for "this has a hardware-gated MUST AC that has not been demonstrated." That information exists and is accurate, but it is prose inside a large file rather than a queryable field — see the ecosystem suggestion below.

## Recommendations
- Before `/bmb:archive`, a human should run the documented 8-step bench procedure and record the outcome (pass/fail/deviation) directly in the task file's Resumption Notes or a new "Bench Verification Log" subsection, so the archive's completion record distinguishes "acceptance criteria implemented" from "acceptance criteria demonstrated."
- Treat the `ESP_PLATFORM`/`esp_shim.h` shim as a named, reusable pattern in `systemPatterns.md` (it already is, per the creative doc's Decision 5 cross-reference) — no action needed here beyond confirming that reference stays current as a 3rd/4th instance appears in future tasks.

---

## Claude Code Ecosystem Evaluation

### Commands Assessment
| Command | Used | Effectiveness | Notes |
|---------|------|---------------|-------|
| /bmb:init | N/A (pre-existing project) | — | Not part of this task's lifecycle |
| /bmb:brainstorm | Y | High | Produced roadmap feature + task + creative design in one dialogue; plan-critique ran and caught a real defect (AC-ASYNC-1) despite the compressed path |
| /bmb:plan | N | — | Not run separately — subsumed by `/bmb:brainstorm`. See Workflow Assessment below |
| /bmb:creative | N | — | Not run separately — subsumed by `/bmb:brainstorm` |
| /bmb:build | Y (×3) | High | One dispatch per phase, Sonnet build-orchestrator-agent; all three phases APPROVED with 0 blocking code-review findings and the commit guard passing on first run |
| /bmb:uat | N | N/A | Correctly skipped — firmware-only feature, no web/browser surface in scope |
| /bmb:reflect | Y | (this document) | — |

### Workflow Assessment
- **Phase Progression**: Smooth. Each `/bmb:build` phase had an observable deliverable (host tests only → serial log line → visible driver), matching the creative doc's Decision 8 rationale for why Phase 2 exists at all.
- **Unnecessary Phases**: None. Each phase's boundary maps to a genuine hardware/testability seam (host-testable pure core / host-testable plumbing / device-only driver).
- **Missing Phases/Steps**: The workflow has no first-class step for "hardware bench verification" distinct from the code-agent phases — it is currently represented only as prose in the Resumption Notes and a documented procedure inside the Test Strategy section. For a project with recurring hardware-gated ACs (this is at least the second time — `sensor-monitoring-dashboard`'s reflection flagged the identical "outstanding manual hardware-verification items" pattern), this is a workflow gap worth solving once rather than re-documenting per task. See High-priority suggestion below.

### Context Files Assessment
- **Helpful Files**: `systemPatterns.md` § Pure-Logic/Device-Only Split and § Testing Patterns were load-bearing — the design doc cites them directly to justify Decision 5's module boundaries and the `device_status` host-testability approach, and both citations held up under implementation without needing correction.
- **Gaps Identified**: There is no memory-bank file or section that tracks "hardware-gated verification items pending a human with the physical board" across tasks as a first-class, queryable list. It currently lives as free text inside each task file's Resumption Notes / Test Strategy, which means it disappears from view once a task is archived unless someone re-reads the archived file.
- **Outdated Content**: None found. `lib/device_status/`'s original header comment (recording the LED-consumer blocker) was accurately superseded by this task rather than left stale — worth confirming in a follow-up that the original comment was actually removed/updated in the Phase 2 diff (not verified in this reflection; a quick grep at archive time would close the loop).

### Tools Assessment
| Tool | Usage | Effectiveness | Limitations |
|------|-------|---------------|--------------|
| Read/Grep/Bash | not measured (no per-task session log index available) | — | See Build Session Analysis caveat below |
| Task (sub-agent dispatch) | 3 build-orchestrator dispatches + their internal TDD/reviewer/verifier/doc sub-dispatches | High | Effective — orchestrator applied the one RECOMMENDED code-review finding directly rather than requiring a full re-invocation loop |

### Subagent Assessment
- **Agents Used**: brainstorm critique (anthropic backend), Spec Writer (Sonnet), build-orchestrator-agent ×3 (Sonnet), build-tdd-agent ×3 (Sonnet), build-verifier-agent ×3 (Haiku), build-code-reviewer-agent ×3 (Sonnet), build-documentation-agent ×3 (Haiku).
- **Prompt Quality**: Effective for this task shape. The documentation agent correctly distinguished "this is the Nth application of an existing pattern" (Phase 2, Phase 3 — no `systemPatterns.md` change) from genuinely new content (`techContext.md` updates every phase), avoiding pattern-doc churn for repeated applications of the same architecture.
- **Output Quality**: High across all three phases — code review found real, actionable issues (the RMT cleanup finding) rather than either rubber-stamping or nitpicking.
- **Improvements Needed**: None specific to this task's sub-agent behavior. The one systemic gap (hardware-gated verification tracking) is a workflow/schema issue, not a sub-agent prompt issue.

### Memory Bank Assessment
- **File Structure**: Adequate. The task file's size (585 lines by the final phase) is large but each section stayed navigable and the phase-by-phase Execution State overwrite-in-place convention worked as designed (git history preserves prior phases' detail when needed, as this reflection confirmed by reading `8d4e7fc` and `b01f2e7` directly).
- **Template Usefulness**: The creative design doc's "Decisions" + "Consequences accepted" + "Open bench-verification items" structure is worth calling out as unusually good practice — it turned trade-offs into an explicit, findable checklist rather than leaving them implicit in prose, and this reflection's Creative Decision Assessment section was straightforward to write directly from it.
- **Missing Documents**: See the recurring theme above — no dedicated "pending hardware verification" tracking document/section exists at the project level (only per-task, inside prose).

### Build Session Analysis

**Log availability caveat**: `.agent-logs/claude/by-task/onboard-status-led/` does not exist — only legacy `TASK-001` and `TASK-007` directories are present under `by-task/`. Per the documented fallback, `.agent-logs/claude/2026-08-19/`, `.../2026-08-20/`, and `.../2026-08-21/` date directories exist, but no session-to-slug mapping was attempted for this reflection beyond confirming their presence, since the task's actual work (all three build phases, per the commit timestamps) falls entirely on 2026-08-20/21 and a reliable per-phase attribution would require content-searching every log in those directories for the slug — a cost not justified when the git history and task-file Execution State already provide accurate, phase-scoped ground truth for tool usage, sub-agent dispatch, and outcome. Tool-call and turn-count metrics below are marked **not measured** rather than fabricated.

**Note: by-task log index not available for this task's date range. Run /bmb:init to upgrade session logging** so future Level 3 tasks get the by-task index this reflection had to work around.

- **Build Sessions**: 3 (one `/bmb:build` invocation per phase, per the commit history: `8d4e7fc`, `b01f2e7`, `0fefde5`)
- **Sub-Agents Spawned**: 3 build-orchestrator-agent dispatches, each internally dispatching TDD/verifier/code-reviewer/documentation sub-agents per the Execution State's "Sub-Agents" list — not measured as a raw count beyond what's itemized in the task file
- **Tool Calls**: not measured (no per-task log index)
- **Errors Recovered**: 0 (commit guard passed on first run in all three phases; code review's 2 findings across the whole feature were both applied in-band without requiring a guard-triggered re-invocation)

### Ecosystem Improvement Suggestions

> **IMPORTANT**: These are suggestions only. Do NOT implement changes during reflection.

#### High Priority
1. **Add a first-class "hardware verification pending" status/section distinct from `BUILD_COMPLETE`.** This is the second task in a row (after `sensor-monitoring-dashboard`) where the task file's actual state is "code-complete, all MUST acceptance criteria implemented, but at least one MUST AC is only demonstrable on physical hardware and has not yet been demonstrated." Currently this is represented as prose inside Resumption Notes, discoverable only by reading the whole task file. A dedicated frontmatter field (e.g., `bench_verification: pending | passed | failed`) or a required `### Bench Verification Log` section — checked by `/bmb:archive`'s phase gate the same way reflection-doc existence is checked — would make this a queryable, enforceable signal instead of relying on a human reading prose carefully before merging.
2. **Enable per-task session log indexing (`.agent-logs/claude/by-task/<slug>/`) for this project.** This is the second reflection in a row that had to fall back to a caveat rather than measure actual tool/sub-agent metrics. The `sensor-monitoring-dashboard` reflection already recommended this; it appears not to have been actioned, and the gap recurred identically here.

#### Medium Priority
1. **Add a project-level "Pending Hardware Verification" roll-up** (e.g., `memory-bank/hardware-verification.md` or a section in `productBrief.md`) that aggregates open bench-verification items across all tasks, not just the current one. For a firmware project where every feature seems to end with "needs a human at the bench," a single list surviving across `/bmb:archive` cycles would be more useful than re-discovering the same gap per task's Resumption Notes, which disappear from active view once archived.
2. **Confirm the `/bmb:brainstorm`-as-plan+creative-substitute path is documented as a supported pattern for this task shape** (bounded scope, one pre-existing well-understood seam, a single external unknown resolvable live in conversation), since it worked cleanly here with no loss of plan-critique rigor. If this is already implicit in the command design, no change needed beyond noting the evidence in this reflection.

#### Low Priority
1. **Cross-reference `led_strip_encoder.{h,c}`'s vendored provenance from `techContext.md` into a lightweight project-level "Vendored Third-Party Code" inventory** if one doesn't already exist, alongside the `espressif/ds18b20` pinning note — both are attribution/licensing-relevant facts currently living in prose inside `techContext.md`'s changelog rather than in one place a license audit could scan quickly.

## References
- Plan: memory-bank/tasks/onboard-status-led.md (feature branch tip, `feature/onboard-status-led`)
- Creative: memory-bank/creative/onboard-status-led-design.md
- Roadmap: memory-bank/roadmap/onboard-status-led.md
- Timeline: `git log` on `feature/onboard-status-led` — `1574f85` (brainstorm/design approved), `8d4e7fc` (Phase 1), `b01f2e7` (Phase 2), `0fefde5` (Phase 3)

---

## Key Learnings

### Extractable Learnings (for Continuous Learning)

1. - **testing-patterns** (`lib/*` device-touching modules, ESP-IDF firmware): Apply the `ESP_PLATFORM`/`esp_shim.h` conditional-include pattern by default when a device-touching module needs host-test coverage, rather than re-deriving a host-testability approach per module.
2. - **acceptance-criteria** (`memory-bank/tasks/*.md`, hardware-gated ACs): State a hardware-verification threshold in terms of an observable event's occurrence (e.g., "first illumination"), never in terms of a Kconfig-tunable cadence (e.g., a blink edge), so a legal configuration value cannot fail a MUST-priority AC.
3. - **test-strategy** (firmware phases with no host-testable surface): Write the "what NOT to test and why" list into the Test Strategy during planning, before the phase that needs the exemption, so a zero-new-tests phase is a pre-committed decision rather than a build-time justification.

### Learned Rules Applied

- No learned rules were read for this task (`memory-bank/agent-rules/_learned/` was not consulted as part of this reflection — the creative design doc cites `systemPatterns.md`'s `testability-patterns` guidance directly instead, which is human-authored project guidance, not an auto-extracted learned rule). If `_learned/` rules exist on `metadata_branch` at archive time, this section should be revisited then.

### For Claude Code Workflow

1. The compressed `/bmb:brainstorm` path deserves a follow-up data point: track whether plan-critique finding rates differ meaningfully between brainstorm-compressed tasks and separate plan/creative tasks over the next several Level 3 features, to confirm the "no loss of rigor" observation here generalizes rather than being a one-task anecdote.
2. The recurring "outstanding bench verification" pattern across two consecutive reflections (`sensor-monitoring-dashboard`, now `onboard-status-led`) is itself a signal the ecosystem should act on rather than re-flag a third time — see High Priority suggestion #1.
3. Sub-agent dispatch continues to work well when the orchestrator is trusted to apply a single RECOMMENDED code-review finding directly rather than forcing a full re-dispatch loop for a one-line fix — this kept Phase 3 to a single, clean pass.

---

## Conclusion

`onboard-status-led` is a well-executed Level 3 feature: the hard part (a runtime status model that didn't exist) was designed correctly on the first attempt, the tri-state fact model is provably free of the boot-window lying bug it exists to prevent, and all three phases passed code review clean with zero commit-guard flags. The plan-critique gate did real work here, catching a genuine MUST-AC ambiguity (AC-ASYNC-1) before it could reach hardware. The one open item — physical bench verification — is honestly represented in the task file rather than hidden, but its representation (prose in Resumption Notes) is the weakest link in an otherwise strong lifecycle, and this reflection recommends the ecosystem treat that as a solvable workflow gap rather than a per-task footnote, since this is the second consecutive feature to hit it.

**Overall Task Success**: ⚠️ Partial Success — implementation and host-verifiable acceptance criteria are fully met; hardware-gated acceptance criteria (AC-INTEGRATION-1, AC-ASYNC-1's bench half, AC-VERIFY-6's GPIO confirmation) remain implemented-but-undemonstrated pending a human with the physical board.

**Overall Workflow Effectiveness**: ✅ Highly Effective — the brainstorm-compressed path, phased build, and sub-agent chain all performed well with zero guard flags and zero re-invocations across three phases.

**Recommendation**: Ready to archive once a human completes the documented 8-step bench procedure and records the outcome in the task file; if archiving proceeds before that (e.g., to unblock other work), the archive record should explicitly flag AC-INTEGRATION-1 and the bench items as pending rather than closed.
