# Agent Rules Index

Generated: 2026-08-20T17:20:00-04:00
Indexed: 3 rules (0 human-authored, 3 learned) | Rejected: 0 (unsafe) | Warnings: 0

## Validation Summary

### Health Check
- Total rules: 3
- Human-authored rules: 0
- Learned rules (auto-generated): 3
- Estimated max context: ~98 lines (OK)
- Conflicts detected: 0

Worst-case context load is `src/Kconfig.projbuild`, which matches all three rules
(30 + 34 + 34 = 98 lines) — well under the 300-line warning threshold. No file pattern
matches more than 3 rules.

### ⚠️ Warnings
None.

### 🚫 Rejected Rules (Unsafe)
None.

> Scan note: the safety scan produced one substring false positive — `config-drift.md:19`
> contains "the **upload tool**", which matches the `upload to` data-exfiltration pattern.
> The surrounding text is ESP-IDF build tooling terminology, not an exfiltration
> instruction. Rule retained.

### Provenance
All three rules are auto-generated (`auto_generated: true`) from the
`sensor-monitoring-dashboard` reflection, consolidated at `/bmb:archive` on 2026-08-20.
All start at `priority: low` with `evidence_count: 1`, so they never override
human-authored rules. Promote a rule by raising its `priority` to `medium` or `high`;
delete the file (or specific bullets) and re-run `/bmb:rules-index` to remove one.

---

## Rules by File Pattern

| Pattern | Rule | Priority | Lines |
|---------|------|----------|-------|
| `platformio.ini` | [build-verification.md](agent-rules/_learned/build-verification.md) | low | 34 |
| `platformio.ini` | [config-drift.md](agent-rules/_learned/config-drift.md) | low | 30 |
| `CMakeLists.txt` | [build-verification.md](agent-rules/_learned/build-verification.md) | low | 34 |
| `*.cmake` | [build-verification.md](agent-rules/_learned/build-verification.md) | low | 34 |
| `sdkconfig*` | [build-verification.md](agent-rules/_learned/build-verification.md) | low | 34 |
| `sdkconfig*` | [config-drift.md](agent-rules/_learned/config-drift.md) | low | 30 |
| `**/idf_component.yml` | [build-verification.md](agent-rules/_learned/build-verification.md) | low | 34 |
| `*.csv` | [config-drift.md](agent-rules/_learned/config-drift.md) | low | 30 |
| `Kconfig*` | [config-drift.md](agent-rules/_learned/config-drift.md) | low | 30 |
| `**/Kconfig.projbuild` | [config-drift.md](agent-rules/_learned/config-drift.md) | low | 30 |
| `lib/**` | [testability-patterns.md](agent-rules/_learned/testability-patterns.md) | low | 34 |
| `src/**` | [testability-patterns.md](agent-rules/_learned/testability-patterns.md) | low | 34 |
| `test/**` | [testability-patterns.md](agent-rules/_learned/testability-patterns.md) | low | 34 |

## Rules by Path

| Path Contains | Rule | Priority | Lines |
|---------------|------|----------|-------|
| `lib/` | [build-verification.md](agent-rules/_learned/build-verification.md) | low | 34 |
| `lib/` | [testability-patterns.md](agent-rules/_learned/testability-patterns.md) | low | 34 |
| `src/` | [build-verification.md](agent-rules/_learned/build-verification.md) | low | 34 |
| `src/` | [testability-patterns.md](agent-rules/_learned/testability-patterns.md) | low | 34 |
| `test/` | [testability-patterns.md](agent-rules/_learned/testability-patterns.md) | low | 34 |

## Rules by Topic

| Keywords | Rule | Priority |
|----------|------|----------|
| build, embedded, linking, toolchain, verification | [build-verification.md](agent-rules/_learned/build-verification.md) | low |
| configuration, embedded, esp-idf, platformio, build | [config-drift.md](agent-rules/_learned/config-drift.md) | low |
| testing, architecture, modularity, tdd, embedded | [testability-patterns.md](agent-rules/_learned/testability-patterns.md) | low |

---

## Conflict Resolutions

None. The three rules overlap on patterns but not on instructions:

| Overlap | Rules | Assessment |
|---------|-------|------------|
| `platformio.ini`, `sdkconfig*` | build-verification + config-drift | Additive — one covers link-time artifact verification, the other config propagation between tooling layers |
| `lib/`, `src/` | build-verification + testability-patterns | Additive — one covers whether a module reached the image, the other where to split it |
| topic `embedded` | all three | Additive — no contradictory directives |

All three share `priority: low`, but since no instructions conflict there is nothing to
resolve by priority and no UNRESOLVED entries.
