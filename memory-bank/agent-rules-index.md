# Agent Rules Index

Generated: 2026-08-21T17:30:00-04:00
Indexed: 4 rules (0 human-authored, 4 learned) | Rejected: 0 (unsafe) | Warnings: 0

## Validation Summary

### Health Check
- Total rules: 4
- Human-authored rules: 0
- Learned rules (auto-generated): 4
- Estimated max context: ~119 lines (OK)
- Conflicts detected: 0

Worst-case context load is `src/Kconfig.projbuild`, which matches three rules
(build-verification 44 + config-drift 30 + testability-patterns 45 = 119 lines) — under the
300-line warning threshold. `planning-specification.md` scopes to `memory-bank/tasks/` and
`memory-bank/creative/`, so it never stacks with the three source-tree rules. No file
pattern matches more than 3 rules.

### ⚠️ Warnings
None.

### 🚫 Rejected Rules (Unsafe)
None.

> Scan note: the safety scan produced one substring false positive — `config-drift.md:19`
> contains "the **upload tool**", which matches the `upload to` data-exfiltration pattern.
> The surrounding text is ESP-IDF build tooling terminology, not an exfiltration
> instruction. Rule retained.

### Provenance
All four rules are auto-generated (`auto_generated: true`) and consolidated at
`/bmb:archive`.

| Rule | derived_from | evidence_count | last_validated |
|------|--------------|----------------|----------------|
| build-verification.md | sensor-monitoring-dashboard, onboard-status-led | 2 | 2026-08-21 |
| config-drift.md | sensor-monitoring-dashboard | 1 | 2026-08-20 |
| testability-patterns.md | sensor-monitoring-dashboard, onboard-status-led | 2 | 2026-08-21 |
| planning-specification.md | onboard-status-led | 1 | 2026-08-21 |

All remain at `priority: low` (promotion threshold is `evidence_count >= 3`), so they never
override human-authored rules. Promote a rule by raising its `priority` to `medium` or
`high`; delete the file (or specific bullets) and re-run `/bmb:rules-index` to remove one.
Audit trail: `git log -- memory-bank/agent-rules/_learned/`.

---

## Rules by File Pattern

| Pattern | Rule | Priority | Lines |
|---------|------|----------|-------|
| `platformio.ini` | [build-verification.md](agent-rules/_learned/build-verification.md) | low | 44 |
| `platformio.ini` | [config-drift.md](agent-rules/_learned/config-drift.md) | low | 30 |
| `CMakeLists.txt` | [build-verification.md](agent-rules/_learned/build-verification.md) | low | 44 |
| `*.cmake` | [build-verification.md](agent-rules/_learned/build-verification.md) | low | 44 |
| `sdkconfig*` | [build-verification.md](agent-rules/_learned/build-verification.md) | low | 44 |
| `sdkconfig*` | [config-drift.md](agent-rules/_learned/config-drift.md) | low | 30 |
| `**/idf_component.yml` | [build-verification.md](agent-rules/_learned/build-verification.md) | low | 44 |
| `*.csv` | [config-drift.md](agent-rules/_learned/config-drift.md) | low | 30 |
| `Kconfig*` | [config-drift.md](agent-rules/_learned/config-drift.md) | low | 30 |
| `**/Kconfig.projbuild` | [config-drift.md](agent-rules/_learned/config-drift.md) | low | 30 |
| `lib/**` | [testability-patterns.md](agent-rules/_learned/testability-patterns.md) | low | 45 |
| `src/**` | [testability-patterns.md](agent-rules/_learned/testability-patterns.md) | low | 45 |
| `test/**` | [testability-patterns.md](agent-rules/_learned/testability-patterns.md) | low | 45 |
| `memory-bank/tasks/*.md` | [planning-specification.md](agent-rules/_learned/planning-specification.md) | low | 39 |
| `memory-bank/creative/*.md` | [planning-specification.md](agent-rules/_learned/planning-specification.md) | low | 39 |

## Rules by Path

| Path Contains | Rule | Priority | Lines |
|---------------|------|----------|-------|
| `lib/` | [build-verification.md](agent-rules/_learned/build-verification.md) | low | 44 |
| `lib/` | [testability-patterns.md](agent-rules/_learned/testability-patterns.md) | low | 45 |
| `src/` | [build-verification.md](agent-rules/_learned/build-verification.md) | low | 44 |
| `src/` | [testability-patterns.md](agent-rules/_learned/testability-patterns.md) | low | 45 |
| `test/` | [testability-patterns.md](agent-rules/_learned/testability-patterns.md) | low | 45 |
| `memory-bank/tasks/` | [planning-specification.md](agent-rules/_learned/planning-specification.md) | low | 39 |
| `memory-bank/creative/` | [planning-specification.md](agent-rules/_learned/planning-specification.md) | low | 39 |

## Rules by Topic

| Keywords | Rule | Priority |
|----------|------|----------|
| build, embedded, linking, toolchain, verification | [build-verification.md](agent-rules/_learned/build-verification.md) | low |
| configuration, embedded, esp-idf, platformio, build | [config-drift.md](agent-rules/_learned/config-drift.md) | low |
| testing, architecture, modularity, tdd, embedded | [testability-patterns.md](agent-rules/_learned/testability-patterns.md) | low |
| planning, acceptance-criteria, test-strategy, specification, embedded | [planning-specification.md](agent-rules/_learned/planning-specification.md) | low |

---

## Conflict Resolutions

None. The four rules overlap on patterns but not on instructions:

| Overlap | Rules | Assessment |
|---------|-------|------------|
| `platformio.ini`, `sdkconfig*` | build-verification + config-drift | Additive — one covers link-time artifact verification, the other config propagation between tooling layers |
| `lib/`, `src/` | build-verification + testability-patterns | Additive — one covers whether a module reached the image, the other where to split it |
| topic `embedded` | all four | Additive — no contradictory directives |
| zero-test phases | testability-patterns + planning-specification | Complementary, and deliberately so — testability-patterns says *what to substitute* for tests when a phase has no host-testable surface (a regression gate); planning-specification says *when to decide it* (at planning time, pre-committed). Different halves of the same lesson, applied at different phases and to different files. |

All four share `priority: low`, but since no instructions conflict there is nothing to
resolve by priority and no UNRESOLVED entries.

No file exceeds the 15-bullet cap (max: testability-patterns at 8) and no rule is
`superseded_by` another — this consolidation was purely additive: 2 new bullets appended to
each of `build-verification.md` and `testability-patterns.md`, 1 new file created. Nothing
was merged, retired, expired, or pruned.
