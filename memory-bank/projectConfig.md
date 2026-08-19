# Project Configuration

`memory-bank/projectConfig.md` holds **machine-config only** — settings banyan reads for routing and automation: version stamp, git/branching keys, the team roster, and UAT defaults. It carries **no** reference or narrative content.

> Component structure, development commands, and test execution strategy live in `memory-bank/techContext.md`. Project foundation and product context live in `memory-bank/productBrief.md`.

## Banyan Memory Bank

This section is auto-managed by `/bmb:init`. Do not edit manually.

- **Banyan Version**: 2.2.1
- **Initialized**: 2026-08-19
- **Last Updated**: 2026-08-19

## Git & Branching (v2)

Read by every banyan command for branch routing and protected-branch enforcement (see `context/branch-routing.md`).

```yaml
metadata_branch: main            # where memory-bank truth lives
protected_branches: [main]       # banyan never commits to these — all writes go via PR
pr_target: main                  # banyan PRs target this
sync_automation: none            # status record for /bmb:doctor (NOT a mode switch)
archive_strategy: push-and-pr
worktree_root: ~/banyan-wt/hydroponic-monitor/
```

Classic single-branch mode: `metadata_branch` is a protected branch and bookkeeping reaches it via PR.

## Agent Backends

Which execution backend drives each configurable **seam** of the workflow. BMB runs on Anthropic
(Claude) by default; a project may route individual content-producing seams to the optional
`codex@openai-codex` plugin instead. **This block is the per-project source of truth — edit it here
to override backends for this project.**

Each value is `<provider>[:<model>]` — `anthropic[:haiku|sonnet|opus]` or `codex[:<codex-model>]`:

```yaml
backends:
  plan:                  anthropic      # anthropic[:tier] | codex[:model]   (codex not recommended)
  tdd:                   anthropic      # anthropic[:tier] | codex[:model]   — build RED→GREEN implementation
  code-review:           anthropic      # anthropic[:tier] | codex[:model]   — build Step 8 quality gate
  creative-architecture: anthropic      # anthropic[:tier] | codex[:model]   — system-design doc
  creative-uiux:         anthropic      # anthropic[:tier] | codex[:model]   — UI/UX design doc
  creative-algorithm:    anthropic      # anthropic[:tier] | codex[:model]   — algorithm design doc
  creative-user-journey: anthropic      # anthropic[:tier] | codex[:model]   — user-journey doc
  creative-critique:     codex          # anthropic[:tier] | codex[:model] | off  — adversarial pass
  auto-final-review:     anthropic      # anthropic[:tier] | codex[:model]   — auto-build final review
  availability:          auto           # auto = silently fall back to Anthropic if Codex is down
                                         # on   = fall back the same way, but emit a visible ⚠ warning
# tier ∈ haiku|sonnet|opus — tunes the Claude sub-agent that runs the seam.
```

Codex was **not detected** on this machine at init, so every mandatory seam is `anthropic` and the
optional `creative-critique` pass simply skips — behavior identical to pre-integration BMB. If the
`codex@openai-codex` plugin is installed later, the critique pass self-enables; re-run
`/bmb:doctor` to confirm, and flip `code-review` / `auto-final-review` to `codex` by hand if you
want an independent reviewer.

## Team

Maps each contributor's git identity (email) to a friendly first name. Used for owner attribution and every human-facing display (`/bmb:go` status, the context line, `@you`).

```yaml
team:
  # <git-email>: <friendly first name> — self-populating; see
  # context/branch-routing.md § Team Roster. Do not collect the full team here.
  92587431+MrMophandle@users.noreply.github.com: Ryan
```

If the map is absent or has no entry for an author, displays fall back to the raw git author name (`%an`).

## UAT

Project-wide defaults for `/bmb:uat`. Not configured — this project has no web/UI surface today. If a companion dashboard is added later, run `/bmb:uat-init`.

## Notes

- **Build output is committed.** `build/` (415 tracked files of CMake/Ninja/IDF artifacts) is
  tracked while `.pio/` is ignored. Untracking it (`git rm -r --cached build/` + a `.gitignore`
  entry) would make every future diff and PR readable. Left as-is at init — it is the user's call.
- **No CI.** There is no pipeline; `pio run` / `pio test` are local-only.
- **Tests need hardware.** The only test environment targets `esp32-s3-devkitm-1`, so `pio test`
  requires a connected board. This blocks unattended TDD in `/bmb:build` until a `[env:native]`
  (or equivalent host test target) is added to `platformio.ini`.
- **`platformio.ini` pins no versions.** `platform = espressif32` floats to latest; pin it before
  reproducibility matters.
