---
name: Falsifiable Specifications at Planning Time
globs: ["memory-bank/tasks/*.md", "memory-bank/creative/*.md"]
paths: ["memory-bank/tasks/", "memory-bank/creative/"]
topics: ["planning", "acceptance-criteria", "test-strategy", "specification", "embedded"]
priority: low
auto_generated: true
derived_from: [onboard-status-led]
evidence_count: 1
last_validated: 2026-08-21
---

# Falsifiable Specifications at Planning Time

An acceptance criterion is only useful if it can be failed for the right reason and cannot be
failed for a wrong one. Both failure modes below are cheap to fix while writing the plan and
expensive to fix once a phase has been built against them.

- State a hardware- or timing-verification threshold in terms of an **observable event's
  occurrence** ("first illumination"), never in terms of a **configurable cadence** ("the
  first blink edge"). A threshold expressed as a cadence can be violated by a perfectly
  legal configuration value on correctly-built firmware — e.g. a MUST-priority "within 1 s"
  AC that a legal `BLINK_MS=2000` breaks. If a Kconfig/env value appears anywhere in the
  reasoning chain behind a threshold, the threshold is measuring the wrong thing.
- Write the **"what NOT to test, and why"** list into the Test Strategy during planning,
  before the phase that needs the exemption. A phase with no host-testable surface (a
  peripheral driver, an RTOS task, anything needing real hardware) is a legitimate
  zero-new-tests phase — but it must be a **pre-committed decision**, not a justification
  offered at build time. Deciding it in advance also makes the resulting acceptance-criteria
  coverage gap visible while there is still time to plan around it.
- When an AC can only be closed by a human at a bench, mark it as such **in the AC itself**,
  not in a notes section. Prose in Resumption Notes does not survive contact with an archive:
  the task reads COMPLETE while a MUST criterion was never demonstrated. Split such an AC
  explicitly into its code-verifiable half and its bench half so the undemonstrated half
  stays visible after the task closes.
- Run the plan-critique pass even on a compressed planning path. On `onboard-status-led`,
  critique of a `/bmb:brainstorm`-compressed plan (no separate `/bmb:plan` + `/bmb:creative`)
  still caught a ship-blocking MUST-AC ambiguity — the compression removed dialogue turns,
  not the need for adversarial review.
