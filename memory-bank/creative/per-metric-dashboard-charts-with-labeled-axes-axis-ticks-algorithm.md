# Algorithm Decision: Per-Metric Dashboard Chart Axes (tick generation & scale selection)

**Created**: 2026-08-23
**Status**: DECIDED
**Decision Type**: Algorithm
**Task**: `per-metric-dashboard-charts-with-labeled-axes` (Level 3)
**Work items unblocked**: WI-001 (numeric scale + ticks), WI-002 (time axis ticks), WI-004 (per-metric plottable predicate + empty-state selection)

---

## Problem Statement

`src/web/app.js` currently draws water temperature and ambient light as two lines on one 900×320
canvas. `plotSeries` (`app.js:125-158`) normalizes each series to its **own** min/max inside a
shared plot area, so the two lines' relative heights and every crossing point are artifacts of two
unrelated normalizations. There are no tick marks, no numeric labels, no units, no time axis
(`drawFrame`, `app.js:77-85`, draws a bare left edge and baseline).

The replacement is three independently-scaled charts — water temperature (°C, line), ambient light
(lux, line), water level (categorical FULL/MID/LOW/FAULT trace, no numeric Y) — sharing one time
X axis.

This document decides **how the tick values, the scale (domain), and the label strings are
computed**, as pure functions in `src/web/dashboard-logic.js`. It does **not** decide how they are
drawn (canvas vs inline SVG, panel layout, level→band form): those are the concurrent UI/UX
agent's decisions (R1, WI-003). Every value this design emits is expressed in **data space or as a
normalized 0..1 fraction — never in pixels** — so a canvas renderer and an SVG renderer consume it
unchanged.

---

## Inputs & Outputs

### Inputs

| Name | Type | Size/Range | Source |
|------|------|------------|--------|
| `values` (one numeric metric) | `Array<number \| null>` | ~180 entries; `temp_c` ≈ 0–40, `lux` ≈ 0–65535 | `buildChartSeries(...).temp_c` / `.lux`, from `GET /api/history?points=180` |
| `t` | `Array<number>` (epoch seconds) | ~180 entries; near-1970 when unsynced | `historyPayload.t` (`reading_json.c:166-192`) |
| `timeValid` | `Array<boolean>` | index-aligned with `t` | `historyPayload.time_valid` |
| `level` | `Array<string>` | `FULL`/`MID`/`LOW`/`FAULT`/`UNKNOWN`, **never `null`** (`reading_json.c:47-58`) | `historyPayload.level` — X axis only, no numeric Y |
| `options.unit` | `string` | `'°C'` \| `'lux'` | caller literal in `app.js` |
| `options.title` | `string?` | `'Water temperature'` \| `'Ambient light'` | caller literal |
| `options.targetTickCount` | `number?` | default 5 (Y), 4 (X) | module constant |

`null` in `lux`/`temp_c` means **gap** (sensor offline for that sample). It is never zero, never
interpolated across, and never contributes to min/max. `buildChartSeries` (`dashboard-logic.js:94-111`)
already passes `null` through unchanged; `finiteRange` (`dashboard-logic.js:119-140`) is the
existing null-safe min/max helper and is **reused as-is**, not re-implemented.

### Outputs

| Name | Type | Description |
|------|------|-------------|
| `MetricAxis` | object | Per-metric state, nice-ed domain, tick values, tick label strings, tick positions (0..1), and pre-split plot segments |
| `TimeAxis` | object | Time-axis state, tick list (`index`, `epochSec`, `x` fraction, `label`), and an honest caption |
| `sampleX(index, n)` | number | The one shared index→X-fraction mapping used by all three charts |

### Edge Cases

| # | Case | Expected behavior |
|---|------|-------------------|
| 1 | `values.length === 0` (no readings recorded yet) | `state: 'no-samples'`, `domain: null`, `ticks: []`, `emptyMessage: 'no readings recorded yet'` |
| 2 | All entries `null` (sensor offline whole window) | `state: 'no-finite-values'`, `domain: null`, `ticks: []`, `emptyMessage: 'no data to plot — sensor offline'`. **Never** a fabricated 0–0 or 0–max range |
| 3 | Exactly one finite value among nulls | Padded, nice-ed domain with the value strictly *inside* it; ≥ 3 ticks; the point is drawn as a 1-point segment |
| 4 | Zero-range series (every finite value identical) | Same as #3 — pad, then nice. Never a degenerate `min === max` domain, never a `range || 1` fudge |
| 5 | Mixed null / finite | Nulls excluded from min/max; **no interpolated tick**; the series splits into contiguous segments at each null |
| 6 | `Infinity` / `NaN` / non-number in `values` | Treated exactly like `null` (`finiteRange`'s `isFinite` gate) |
| 7 | `t.length === 0` | Time axis `state: 'no-samples'`, `ticks: []` |
| 8 | Every `time_valid === false` | Time axis `state: 'clock-not-synced'`, **zero** ticks, `caption: 'clock not synced'`. No epoch-0-derived label ever produced |
| 9 | Mixed `time_valid` | Ticks drawn **only** from the valid subset; caption appends `(earlier samples: clock not synced)` |
| 10 | Only one valid timestamp | Exactly one tick |
| 11 | A tick value that rounds to `-0` | Rendered as `'0'` / `'0.0'`, never `'-0'` |

---

## Constraints

### Performance Requirements

- **Maximum latency**: no stated target. Recompute happens once per successful `/api/history` poll
  (`POLL_INTERVAL_MS = 30000`, `app.js:20,204-211`) on a LAN browser, not on the ESP32.
- **Throughput**: 3 metrics × ~180 samples every 30 s.
- **Memory budget**: browser-side, irrelevant at this N.

**Big-O is not the optimization target here and this document will not pretend otherwise.** At
n = 180 every candidate below is O(n) and completes in tens of microseconds. The real budget is
elsewhere.

### Scale Requirements

- **Current data size**: `points=180` per fetch.
- **Expected growth**: retention window and poll cadence are explicitly out of scope for this task.
  The store holds 2,880 entries (`reading_store_core`), so a future `points=2880` request is the
  realistic 16× ceiling — still trivial for O(n).
- **Peak load**: one browser, one poll every 30 s.

### The Real Constraints

| Constraint | Source | Consequence for this design |
|------------|--------|-----------------------------|
| **Flash/RAM budget** — 30.4% flash / 32.6% RAM today; every line of axis code is embedded in the firmware image via `embed_web_assets.py` | Task file; systemPatterns.md Phase 6 | **Code size is a first-class cost.** Prefer a compact algorithm over an exhaustive one where the difference is not user-visible |
| **No CDN, no chart library, zero dependencies** | Task file; LAN-only device | d3 is unusable as a dependency but is fair prior art to reimplement |
| **Zero npm dependencies in tests** (`node --test test/web/*.test.mjs`, Node v24) | systemPatterns.md § Test Framework | Every decision must be assertable with `node:assert/strict` alone; **no locale-dependent assertions** |
| **Pure-Logic / Device-Only Split** | systemPatterns.md § Design Patterns | Ticks/scale/labels are DOM-free, side-effect-free functions in `dashboard-logic.js`; only drawing lives in `app.js` |
| **Nulls are gaps, never zeros** | Task file; `dashboard-logic.js:94-111` | No `range = max - min \|\| 1` fudge; no zero-coercion |
| **No `console.log`/`console.error`** in `src/web/*.js` | CLAUDE.md § Observability Standards (blocking) | Failures are returned as `state` values, never logged |
| **Rendering-technology agnostic** | R1, concurrent UI/UX agent | Output is data-space values and 0..1 fractions; no pixels, no DOM nodes, no `ctx` |
| **Configuration Is Not Hard-Coded** | systemPatterns.md § Guiding Principles | **Justified deviation** (below) for presentation constants only |

### Guiding-Principle Deviation (declared)

systemPatterns.md § Guiding Principles requires "Pin assignments, thresholds, intervals, and
credentials come from Kconfig or NVS — not string/number literals." The plan pre-authorized a
narrow deviation for **presentation constants in `src/web/*`**, with precedent
(`POLL_INTERVAL_MS = 30000`, `app.js:20`).

This design uses exactly two such literals:

| Literal | Value | Why it is presentation, not configuration |
|---------|-------|-------------------------------------------|
| `Y_AXIS_TARGET_TICKS` | 5 | A hint to the tick stepper about visual density on a ~320 px-tall panel. Changing it changes how crowded the axis looks and nothing else. Not a threshold, not an interval, not a pin |
| `X_AXIS_TARGET_TICKS` | 4 | Same, for a horizontal axis whose labels are ~8–11 characters wide |

**Boundary test applied**: would a bench operator plausibly want to change either? No — they are
consequences of panel geometry, which is the renderer's concern, not the operator's. Both are
overridable per call via `options.targetTickCount` so the concurrent UI/UX agent's layout decision
can tune density without touching this algorithm. Retention window and poll cadence — the two
values that *would* belong in Kconfig — are out of scope and do not appear here.

---

## Options Explored

### Option 1: Extended Wilkinson (Talbot–Lin–Hanrahan) optimization

- **Approach**: Score candidate tick sequences against a weighted objective combining *simplicity*
  (preference for steps drawn from Q = {1, 5, 2, 2.5, 4, 3}), *coverage* (how tightly the domain
  hugs the data), *density* (closeness of the tick count to the target), and *legibility*. Search
  over `(q, j, k)` triples with branch-and-bound pruning and return the highest-scoring sequence.
  This is what a serious plotting library reaches for when axis aesthetics matter.
- **Pseudocode**:
  ```
  best = {score: -inf}
  for j = 1.. while simplicity_max(j) > best.score:
      for q in Q:
          for k = 2..maxTicks:              # candidate tick count
              if density_max(k) < best.score: break
              for each (start, step) with step = q * 10^z:
                  if not covers(data): continue
                  s = w1*simplicity + w2*coverage + w3*density + w4*legibility
                  if s > best.score: best = {start, step, k, score: s}
  return best
  ```
- **Time complexity**: Best O(1) (first candidate wins the bound), Average O(|Q| · K · Z) with
  aggressive pruning ≈ a few hundred score evaluations, Worst O(|Q| · K · Z) unpruned. Independent
  of n after an O(n) min/max pass.
- **Space complexity**: O(k) — one candidate sequence at a time.
- **Data structures**: the constant preference array Q; a mutable best-candidate record.
- **Pros**:
  - Best-in-class axis aesthetics; handles awkward ranges (e.g. 0–65535) with a tighter domain fit
    than any fixed ladder.
  - Tick count lands very near the target.
  - Well-published, well-understood prior art.
- **Cons**:
  - **~150–250 SLOC** of scoring functions and pruning bounds. Against a 30.4%-full flash image
    embedding every byte of `src/web/*`, that is the single largest cost in this comparison.
  - Weight tuning is subjective; tests must assert on *scores* or on hand-computed golden outputs,
    both brittle.
  - Its advantage over Option 2 is invisible at the resolution of a 900 px LAN dashboard: both
    produce round numbers; only the domain padding differs by a few percent.
- **Best For**: publication-quality plots, wide range of unpredictable data scales, plenty of code budget.
- **Worst For**: exactly this project — embedded flash budget, two known-magnitude metrics, no
  designer in the loop to appreciate the difference.

### Option 2: Decimal-magnitude 1/2/5 stepper with a nice-ed domain (d3 `tickIncrement`/`nice` family)

- **Approach**: Derive a raw step from `(hi − lo) / targetCount`, snap it **up** to the nearest
  1 / 2 / 5 × 10^k, then expand the domain outward to the nearest multiples of that step
  (`floor(lo/step)·step`, `ceil(hi/step)·step`) and emit every multiple in between. Label decimals
  are derived from the step's own magnitude, so all labels on an axis carry identical precision.
- **Pseudocode**:
  ```
  function niceStep(span, target):
      raw  = span / target
      mag  = 10 ^ floor(log10(raw))
      norm = raw / mag
      return mag * (norm <= 1 ? 1 : norm <= 2 ? 2 : norm <= 5 ? 5 : 10)

  function niceAxis(lo, hi, target):
      if hi == lo:                          # zero-range / single-value
          pad = abs(lo) * 0.05 or 1
          lo, hi = lo - pad, hi + pad
      step  = niceStep(hi - lo, target)
      dec   = clamp(-floor(log10(step)), 0, 6)
      scale = 10 ^ dec
      i0    = floor(lo / step); i1 = ceil(hi / step)
      ticks = [ round((i0+k) * step * scale) / scale  for k in 0..(i1-i0) ]
      return {domain: [ticks[0], ticks[-1]], step, dec, ticks}
  ```
- **Time complexity**:
  - Best: O(n) — one pass for min/max (`finiteRange`), then O(1) step selection.
  - Average: O(n + k) where k ≤ 7 ticks. n = 180.
  - Worst: O(n + k), same. There is no data-dependent blowup.
- **Space complexity**: O(k) for the tick array plus O(n) for the plot segments (which the renderer
  needs regardless). No auxiliary structures.
- **Data structures**: two plain arrays (ticks, labels) and an array-of-arrays for plot segments.
- **Pros**:
  - **~35 SLOC** for the whole stepper including the zero-range path and float snapping.
  - Every tick is a "human" number (…, 0.5, 1, 2, 5, 10, 20, 50, …) — the property that actually
    matters to the user.
  - **Provably bounded tick count.** Because `step ≥ raw`, the domain spans at most
    `target + 2` steps and at least `target / 2.5` steps ⇒ for `target = 5` the tick count is always
    in **[3, 7]**. That bound is directly assertable as a property test, which is worth more to this
    project than a few percent of domain fit.
  - Integer-grid snapping (`round(i·step·10^dec)/10^dec`) makes tick values **exact** — no
    `0.30000000000000004` ever reaches a label, and tests can assert exact numbers.
  - Decimals derived from the step means no per-unit decimal configuration at all: °C at step 0.5
    gets 1 decimal, lux at step 2000 gets 0, automatically.
- **Cons**:
  - Domain can overshoot the data by up to ~2.5× the raw step in the worst case (e.g. lux 0–65535
    nice-es to 0–80000, a 22% overshoot).
  - Tick count is a *hint*, not a guarantee (bounded, but 3 or 7 rather than exactly 5).
- **Best For**: constrained code budgets, data of predictable magnitude, projects where tests must
  be exact and cheap.
- **Worst For**: dashboards where the domain must hug the data tightly (financial candlesticks,
  scientific error bars).

### Option 3: Fixed-divisor scheme (N equal subdivisions of the raw extent)

- **Approach**: `domain = [min, max]` exactly as measured; ticks at
  `min + i·(max − min)/N` for `i = 0..N`.
- **Pseudocode**:
  ```
  step = (max - min) / N
  ticks = [ min + i*step for i in 0..N ]
  ```
- **Time complexity**: Best/Average/Worst all O(n + N). **Space**: O(N).
- **Data structures**: one array.
- **Pros**:
  - Smallest possible implementation (~6 SLOC) — the absolute floor on flash cost.
  - Exact tick count, always. Domain hugs the data perfectly.
- **Cons**:
  - **Labels are unreadable**: temp 20.13–21.87 over 5 divisions gives ticks at 20.13, 20.478,
    20.826, 21.174, 21.522, 21.87. That is not an axis, it is a list of six arbitrary numbers, and
    it defeats the entire purpose of the task ("numeric tick labels" a bench operator can read at a
    glance).
  - **Ticks jump every poll.** A new sample that nudges `max` by 0.01 °C shifts all six labels.
    Over a 30 s refresh cycle the axis visibly churns — worse than no axis.
  - Still needs a zero-range special case (`max − min == 0` ⇒ step 0 ⇒ all ticks identical), so it
    does not even avoid the edge-case code.
- **Best For**: a sparkline with no labels at all.
- **Worst For**: this task's headline requirement.

### Option 4: Data's own values as ticks (min / median / max)

- **Approach**: Skip synthetic ticks entirely. Emit the series' own min, max, and (optionally) the
  midpoint value as the tick set, labelled with the raw sample values.
- **Pseudocode**:
  ```
  r = finiteRange(values)
  ticks = r ? [r.min, (r.min + r.max)/2, r.max] : []
  ```
- **Time complexity**: O(n) (already paid by `finiteRange`). **Space**: O(1).
- **Data structures**: none beyond the existing `finiteRange` result.
- **Pros**:
  - Smallest of all (~4 SLOC) and reuses `finiteRange` verbatim.
  - Maximally honest: every tick corresponds to a value that was actually measured.
  - Zero-range is trivially handled — one tick.
- **Cons**:
  - Same label ugliness and same per-poll churn as Option 3, plus only 2–3 gridlines, so reading an
    intermediate point off the chart requires mental interpolation between 20.13 and 21.87.
  - A single outlier sample (e.g. one 65535-lux spike from a camera flash) becomes a permanent tick
    label and stretches the axis, with nothing round in between to anchor the eye.
  - Does not generalize to the X axis, where this same idea *is* correct (see the decision) —
    conflating the two would hide that the two axes have genuinely different requirements.
- **Best For**: the **time** axis, where the "ticks must be round numbers" argument does not apply
  and sitting on a real sample is a correctness property.
- **Worst For**: the numeric Y axes.

---

## Complexity Comparison

| Metric | Option 1 (Ext. Wilkinson) | Option 2 (1/2/5 stepper) | Option 3 (fixed divisor) | Option 4 (data values) |
|--------|---------------------------|--------------------------|--------------------------|------------------------|
| Time (Best) | O(n) | O(n) | O(n) | O(n) |
| Time (Avg) | O(n + \|Q\|·K·Z) | O(n + k) | O(n + N) | O(n) |
| Time (Worst) | O(n + \|Q\|·K·Z) | O(n + k) | O(n + N) | O(n) |
| Space | O(k) | O(k) | O(N) | O(1) |
| **Implementation SLOC** | **~200** | **~35** | ~6 | ~4 |
| Tick count control | Near-exact | Bounded [3, 7] | Exact | Fixed at 2–3 |
| Labels are round numbers | Yes | Yes | **No** | **No** |
| Stable across polls | Yes | Yes | **No** | **No** |
| Testable without golden files | Hard | **Easy (exact values)** | Easy | Easy |

**Read this table by the SLOC and the "round numbers / stable" rows, not the complexity rows.** All
four are O(n) at n = 180 with a 30 s recompute cadence; the asymptotic columns discriminate nothing
and are included only to show that they discriminate nothing.

---

## Performance Projection

At the expected scale (180 samples × 3 metrics, once per 30 s, in a LAN browser):

| Option | Expected latency per poll | Memory | Embedded code size (uncompressed, in flash) |
|--------|---------------------------|--------|---------------------------------------------|
| Option 1 | ~200 µs | ~10 KB transient | ~6–8 KB |
| **Option 2** | **~60 µs** | **~8 KB transient** | **~1.4 KB** |
| Option 3 | ~50 µs | ~8 KB transient | ~0.3 KB |
| Option 4 | ~45 µs | ~8 KB transient | ~0.2 KB |

Against a 16 MB flash at 30.4% used, even Option 1's 8 KB is ~0.05% — but Option 2 buys effectively
all of Option 1's user-visible benefit for a fifth of the bytes, and the difference in *test*
complexity is larger than the difference in code size.

---

## Decision

**Chosen for the numeric Y axes: Option 2 — decimal-magnitude 1/2/5 stepper with a nice-ed domain.**

**Chosen for the time X axis: Option 4 (data's own values as ticks), applied to sample indices.**

These are deliberately different choices, because the two axes have different correctness
requirements.

### Rationale — numeric Y (Option 2)

1. **It satisfies the headline requirement at minimum cost.** The task exists because the charts
   have no readable numeric labels. Options 3 and 4 produce labels (`20.478 °C`) that are
   technically numeric and practically useless, and both churn on every 30 s poll. Option 2 is the
   cheapest option that produces round, stable, human-readable ticks.
2. **The flash budget makes Option 1 unjustifiable.** ~200 SLOC of scoring and pruning for a
   difference the bench operator cannot perceive on a 900 px dashboard is exactly the
   over-engineering the plan's "favor a compact algorithm over an exhaustive one where the
   difference is not user-visible" constraint rules out.
3. **It is provable, and therefore cheaply testable with zero dependencies.** The `[3, 7]` tick-count
   bound, the "domain always covers the data" invariant, and the exactness of every tick value are
   all assertable with `node:assert/strict` and no golden files. Given the systemPatterns.md
   testing emphasis on pure logic and the plan's ~12-test Phase 1 budget, testability is a design
   input, not an afterthought.
4. **The step-derived decimal rule removes a whole class of configuration.** No per-unit decimal
   setting, no `°C gets 1, lux gets 0` table to keep in sync with the data. One line
   (`dec = clamp(−floor(log10(step)), 0, 6)`) produces correct precision for both metrics and for
   any future one, and guarantees uniform precision down a single axis.
5. **Cross-contamination is structurally impossible.** `buildMetricAxis` takes **one values array**,
   not the `series` object — so AC-HAPPY-1's "a wild `lux` value must not perturb the `temp_c` axis"
   is not a behavior to test-and-hope, it is a shape the function cannot violate. (The test still
   ships, as a regression guard against a future refactor that reintroduces a `series` parameter.)

### Rationale — time X (Option 4 on indices)

The "ticks must be round numbers" argument does **not** transfer to the time axis, and a
time-domain nice-er (1 m / 5 m / 15 m / 1 h / 6 h ladder with alignment to local hour and midnight
boundaries) would cost ~50 SLOC and introduce timezone-dependent boundary rounding that is
miserable to test without locale assertions — which the existing suite deliberately avoids.

Selecting ticks from the **eligible sample indices** instead buys three properties for ~15 SLOC:

- **AC-ERROR-3 becomes structural.** A tick can only exist at an index whose `time_valid` is `true`,
  because the candidate set *is* the valid-index list. There is no code path that can derive a label
  from an unsynced entry, so no near-1970 date can be produced — not "we remembered to check", but
  "there is nothing to check".
- **Ticks never interpolate.** Every tick sits on a real, measured sample.
- **The X coordinate system is shared by construction.** The plot's X mapping is index-based
  (`i / max(n−1, 1)`, matching `app.js:143`); an index-derived tick lands exactly under the sample
  it names. A time-proportional tick ladder would require converting the *plot* to a time-proportional
  X axis too — a much larger change, out of this task's scope, and one the water-level chart (WI-003,
  UI/UX agent) would also have to adopt.

The sampler now paces with `xTaskDelayUntil()` (systemPatterns.md deviation D2, fixed 2026-08-20),
so index spacing ≈ time spacing and evenly-spaced indices read as evenly-spaced times.

**Precondition, stated per the Pure-Logic / Device-Only Split rule** ("when a pure module's
correctness depends on a timing, ordering, or units property, state it as an explicit precondition
and name the device-only module responsible"): *`buildTimeAxis` assumes samples are evenly spaced in
time. The responsible module is `src/sampler.c`, which honors it via `xTaskDelayUntil()`. If the
sampler ever reverts to `vTaskDelay`, index-spaced ticks silently become time-unevenly-spaced.* This
sentence must appear in the function's doc comment.

### Trade-offs Accepted

| Trade-off | Why acceptable | Mitigation / future option |
|-----------|----------------|----------------------------|
| Domain can overshoot the data by up to ~2.5× the step (lux 0–65535 → 0–80000, 22% wasted panel height) | Round labels and a bounded tick count are worth more than a tight fit on a monitoring dashboard where absolute values, not fine deltas, are what the operator reads | If bench feedback objects, switch `niceStep`'s thresholds to d3's √2 / √10 / √50 ladder (a one-line change) for a tighter fit at the cost of the [3, 7] bound |
| Tick count is 3–7, not exactly 5 | Bounded and provable; visually indistinguishable on a 320 px panel | None needed |
| A constant-zero series yields the domain `[−1, 1]`, so a lux axis briefly shows negative ticks | Occurs only when **all 180 samples are exactly 0.0**. It is not dishonest — the axis shows a padded neighborhood of a constant, with the flat line at the visual center, which is the clearest possible reading of "this value never changed". A one-sided or clamped domain would put the line on the baseline where it is hard to distinguish from the axis itself | If bench feedback objects, add an opt-in `options.nonNegative` that clamps `domain.min` to 0 when `dataRange.min >= 0`. Deliberately **not** shipped now: one extra option, one extra code path, one extra test, for a case that requires a permanently-stuck sensor |
| Time ticks are wall-clock instants (`10:13:20 PM`) rather than round times (`10:15`) | Every tick sits on a real sample, which is the stronger honesty property, and it costs ~35 fewer SLOC and zero timezone logic | A time-domain nice-er can be added later *if* the plot's X mapping is also converted to time-proportional; both must change together |
| Tick labels carry no thousands separator (`65000`, not `65,000`) | `toLocaleString` would make label text locale-dependent, and the existing suite deliberately never asserts locale output (`dashboard-logic.test.mjs`, `formatReadingTimestamp` tests assert only *type* and *non-emptiness*). BH1750's ceiling is 5 digits, which fits | If needed, a hand-rolled grouping function — but then assert it with an explicit locale-free implementation, never `toLocaleString` |

---

## Implementation Details

### Module constants (`src/web/dashboard-logic.js`)

```js
var Y_AXIS_TARGET_TICKS = 5;   // presentation constant — see § Guiding-Principle Deviation
var X_AXIS_TARGET_TICKS = 4;   // presentation constant — wider labels, fewer ticks
var MAX_LABEL_DECIMALS  = 6;   // float-snapping guard, not a display preference
```

### Public API — pin these names, signatures, and shapes

#### 1. `buildMetricAxis(values, options)` — WI-001 **and** WI-004's per-metric predicate

```js
/**
 * @param {Array<number|null>} values   one metric's series (series.temp_c or series.lux)
 * @param {{ unit: string, title?: string, targetTickCount?: number }} options
 * @returns {MetricAxis}
 */
```

```js
// MetricAxis — state 'ok'
{
  state: 'ok',
  unit: '°C',
  axisTitle: 'Water temperature (°C)',     // options.title ? title + ' (' + unit + ')' : unit
  emptyMessage: null,
  dataRange: { min: 20.13, max: 21.87, count: 174 },   // verbatim finiteRange() result
  domain:    { min: 20,    max: 22 },
  step: 0.5,
  decimals: 1,
  ticks:         [20, 20.5, 21, 21.5, 22],
  tickLabels:    ['20.0', '20.5', '21.0', '21.5', '22.0'],
  tickPositions: [0, 0.25, 0.5, 0.75, 1],
  segments: [
    [ { index: 0, value: 20.4, x: 0,      y: 0.20 },
      { index: 1, value: 20.9, x: 0.0056, y: 0.45 } ],   // run ends at a null
    [ { index: 3, value: 21.2, x: 0.0168, y: 0.60 } ]    // next run
  ]
}
```

```js
// MetricAxis — state 'no-samples'  (values.length === 0)
{
  state: 'no-samples',
  unit: '°C',
  axisTitle: 'Water temperature (°C)',
  emptyMessage: 'no readings recorded yet',
  dataRange: null, domain: null, step: null, decimals: null,
  ticks: [], tickLabels: [], tickPositions: [], segments: []
}

// MetricAxis — state 'no-finite-values'  (length > 0, every entry null/NaN/Infinity)
{
  state: 'no-finite-values',
  unit: 'lux',
  axisTitle: 'Ambient light (lux)',
  emptyMessage: 'no data to plot — sensor offline',
  dataRange: null, domain: null, step: null, decimals: null,
  ticks: [], tickLabels: [], tickPositions: [], segments: []
}
```

**Field contracts:**

- `state` is the **only** discriminator; there is deliberately no redundant `plottable` boolean.
  Callers `switch` on it, which is what makes AC-ERROR-1 / AC-ERROR-2's two empty cases impossible
  to collapse into one.
- `emptyMessage` is `null` iff `state === 'ok'`. Copy is pinned:
  `'no readings recorded yet'` (matches existing `app.js:114`) and
  `'no data to plot — sensor offline'` (**singular** "sensor", changed from the current
  `'no data to plot — sensors offline'` because each chart now speaks for one sensor).
- `ticks[0] === domain.min` and `ticks[ticks.length−1] === domain.max`, therefore
  `tickPositions[0] === 0` and the last is `1`. `tickPositions` is redundant with `domain` on
  purpose — see § R4 Mitigation.
- **Fraction convention**: `y = (value − domain.min) / (domain.max − domain.min)`, so **`y = 0` is
  `domain.min` (chart bottom) and `y = 1` is `domain.max` (chart top)**. The renderer flips into
  pixel space. `tickPositions` uses the identical convention, so ticks and data share one coordinate
  system by construction. `x = index / max(n − 1, 1)`, matching the existing `app.js:143` mapping.
- `segments` is an array of contiguous finite runs. A `null` / `NaN` / `Infinity` entry **ends** the
  current run; the next finite entry **starts** a new one. Empty runs are never emitted, so
  leading/trailing nulls produce no empty arrays. A single isolated finite value produces a
  one-point segment (the renderer draws a dot, not a line).
- `dataRange` is `finiteRange(values)` verbatim, exposed so `buildChartAriaLabel` can keep using the
  true measured min/max rather than the padded domain.

#### 2. `buildTimeAxis(series, options)` — WI-002

```js
/**
 * @param {ChartSeries} series          from buildChartSeries (see change below)
 * @param {{ targetTickCount?: number }} [options]
 * @returns {TimeAxis}
 */
```

```js
// TimeAxis — state 'ok'
{
  state: 'ok',
  sampleCount: 180,
  validCount: 180,
  ticks: [
    { index: 0,   epochSec: 1700000000, x: 0,     label: '10:13:20 PM' },
    { index: 60,  epochSec: 1700001800, x: 0.335, label: '10:43:20 PM' },
    { index: 120, epochSec: 1700003600, x: 0.670, label: '11:13:20 PM' },
    { index: 179, epochSec: 1700005370, x: 1,     label: '11:42:50 PM' }
  ],
  caption: '11/14/2023, 10:13:20 PM – 11/14/2023, 11:42:50 PM'
}

// TimeAxis — state 'clock-not-synced'  (n > 0, every time_valid false)
{ state: 'clock-not-synced', sampleCount: 180, validCount: 0,
  ticks: [], caption: 'clock not synced' }

// TimeAxis — state 'no-samples'  (n === 0)
{ state: 'no-samples', sampleCount: 0, validCount: 0,
  ticks: [], caption: 'no readings recorded yet' }

// TimeAxis — state 'ok' with a partially-unsynced window (validCount < sampleCount)
{ state: 'ok', sampleCount: 180, validCount: 42,
  ticks: [ /* only indices with time_valid === true */ ],
  caption: '11/14/2023, 11:22:00 PM – 11/14/2023, 11:42:50 PM (earlier samples: clock not synced)' }
```

**Field contracts:**

- `ticks[].index` is an index into the **full** window (length `sampleCount`), and
  `x = index / max(sampleCount − 1, 1)` — *not* an index into the valid subset. This is what makes a
  valid-suffix window's ticks land under the correct samples.
- `ticks[].label` comes from `formatAxisTimestamp(epochSec, true)` (below). Never derived from an
  invalid entry.
- `caption` is **always a non-empty string** — there is no null-caption branch for the renderer to
  forget. It carries the date context that time-only tick labels lack, and it carries the
  partial-sync disclosure.
- `validCount === 0 && sampleCount > 0` ⟺ `state === 'clock-not-synced'`. This is the explicit
  not-synced marker AC-ERROR-3 requires, mirroring `app.js:195`'s
  `'last updated (clock not synced)'`.

#### 3. `sampleX(index, sampleCount)` — the one shared X mapping

```js
function sampleX(index, sampleCount) {
  return index / Math.max(sampleCount - 1, 1);
}
```

Exported so the **water-level renderer (WI-003)** and the time-axis renderer use the identical
mapping the numeric charts' `segments` already carry. This is the one thing the level chart must
share with the time axis; everything else about level→band segmentation is the UI/UX agent's
decision. Two SLOC to eliminate a three-way drift risk.

#### 4. `formatAxisTimestamp(epochSec, timeValid)` — compact tick label

`formatReadingTimestamp`'s `toLocaleString()` output ("11/14/2023, 10:13:20 PM") is far too wide for
four X ticks. The **honesty rule** is factored into a shared private guard so it cannot diverge, and
neither function hand-rolls date arithmetic:

```js
function formatEpoch(epochSec, timeValid, timeOnly) {
  if (!timeValid) { return null; }                       // the one honesty gate
  var d = new Date(epochSec * 1000);
  return timeOnly ? d.toLocaleTimeString() : d.toLocaleString();
}
function formatReadingTimestamp(epochSec, v) { return formatEpoch(epochSec, v, false); } // UNCHANGED behavior
function formatAxisTimestamp(epochSec, v)    { return formatEpoch(epochSec, v, true);  }
```

`formatReadingTimestamp`'s observable behavior is byte-identical, so its three existing tests stay
green untouched.

#### 5. `buildChartSeries` — one additive change

`buildChartSeries` currently discards `t` and `time_valid` after building `labels`
(`dashboard-logic.js:101-110`). `buildTimeAxis` needs them. Add two pass-through fields:

```js
return {
  labels: labels,
  t: t.slice(),                 // NEW
  timeValid: timeValid.slice(), // NEW
  lux: lux.slice(),
  temp_c: tempC.slice(),
  level: level.slice(),
};
```

Verified safe against the 25-test floor: no existing test does a whole-object `deepEqual` on the
return value (`dashboard-logic.test.mjs:150-186` asserts individual fields only).

#### 6. `hasPlottableData` — retained, re-rooted, superseded

Kept exported so its existing tests stay green, but its doc comment is updated to record that
`buildMetricAxis(...).state` is its per-metric successor and that `app.js` no longer calls it. It
continues to delegate to the private `finiteRange`, which remains the **single** `isFinite`
predicate in the module. `buildChartAriaLabel` is unaffected.

### Algorithm Steps

**`buildMetricAxis(values, options)`**

1. If `!values || values.length === 0` → return the `'no-samples'` shape. *(Distinguishes AC-ERROR-1
   from AC-ERROR-2 before any numeric work.)*
2. `r = finiteRange(values)`. If `r === null` → return the `'no-finite-values'` shape. *(AC-HAPPY-2:
   an all-null series claims no range; nothing is fabricated.)*
3. `lo = r.min; hi = r.max`. If `lo === hi` → `pad = Math.abs(lo) * 0.05 || 1; lo -= pad; hi += pad`.
   *(Handles single-value and zero-range identically; the `|| 1` catches the constant-zero case.)*
4. `step = niceStep(hi − lo, options.targetTickCount || Y_AXIS_TARGET_TICKS)`.
5. `decimals = clamp(−Math.floor(Math.log10(step)), 0, MAX_LABEL_DECIMALS)`;
   `scale = Math.pow(10, decimals)`. *(Derives label precision from the step; guarantees uniform
   precision down the axis.)*
6. `i0 = Math.floor(lo / step)`, `i1 = Math.ceil(hi / step)`; emit
   `ticks[k] = Math.round((i0 + k) * step * scale) / scale` for `k = 0 .. i1 − i0`.
   *(Integer-grid snapping. Because `step` is 1/2/5 × 10^e and `decimals = max(0, −e)`,
   `step * scale` is exactly 1, 2, or 5 — every tick is exact, no float noise reaches a label.)*
7. `domain = { min: ticks[0], max: ticks[ticks.length − 1] }`;
   `span = domain.max − domain.min`.
8. `tickLabels[k] = formatTickLabel(ticks[k], decimals)`;
   `tickPositions[k] = (ticks[k] − domain.min) / span`.
9. Walk `values` once building `segments`: for each `i`, if the entry is a finite number push
   `{ index: i, value: v, x: sampleX(i, n), y: (v − domain.min) / span }` onto the open run,
   otherwise close the open run. *(The single gap rule, applied once, in tested pure code.)*
10. Return the `'ok'` shape.

**`niceStep(span, target)`**

1. `raw = span / target`.
2. `mag = Math.pow(10, Math.floor(Math.log10(raw)))`; `norm = raw / mag`.
3. `return mag * (norm <= 1 ? 1 : norm <= 2 ? 2 : norm <= 5 ? 5 : 10)`.

*Snapping **up** is what yields the provable bound: `step ≥ raw` ⇒ at most `target + 1` intervals
after the floor/ceil expansion; `step ≤ 2.5 · raw` ⇒ at least `target / 2.5` intervals. For
`target = 5`: **3 ≤ ticks.length ≤ 7**.*

**`formatTickLabel(value, decimals)`**

1. `if (value === 0) { value = 0; }` — assigns `+0`, so a `−0` from step arithmetic can never print
   as `'-0'`.
2. `return value.toFixed(decimals)`.

*No thousands separator, no `k` suffix, no `toLocaleString` — see § Trade-offs Accepted.*

**`buildTimeAxis(series, options)`**

1. `t = series.t || []`, `n = t.length`. If `n === 0` → `'no-samples'` shape.
2. Build `E`, the array of indices where `series.timeValid[i] === true` **and** `t[i]` is a finite
   number. *(This candidate set is what makes AC-ERROR-3 structural.)*
3. If `E.length === 0` → `'clock-not-synced'` shape (`ticks: []`, `caption: 'clock not synced'`).
4. `target = options.targetTickCount || X_AXIS_TARGET_TICKS`.
   If `E.length <= target` → `chosen = E`.
   Else → `chosen[j] = E[Math.round(j * (E.length − 1) / (target − 1))]` for `j = 0 .. target − 1`.
   *(Always includes the first and last valid index; no duplicates are possible because
   `E.length > target`.)*
5. For each chosen index `i`: `{ index: i, epochSec: t[i], x: sampleX(i, n),
   label: formatAxisTimestamp(t[i], true) }`.
6. `caption = formatReadingTimestamp(t[E[0]], true) + ' – ' + formatReadingTimestamp(t[E[E.length−1]], true)`,
   plus `' (earlier samples: clock not synced)'` when `E.length < n`.
7. Return the `'ok'` shape with `sampleCount: n`, `validCount: E.length`.

### Data Structures

| Structure | Purpose | Operations used |
|-----------|---------|-----------------|
| `Array<number\|null>` (input series) | The metric samples | one O(n) scan in `finiteRange`, one O(n) scan for segments |
| `ticks: Array<number>` | Nice-ed tick values | O(k) build, k ≤ 7 |
| `tickLabels` / `tickPositions` | Index-aligned strings and 0..1 fractions | O(k) map |
| `segments: Array<Array<Point>>` | Contiguous finite runs; the renderer's pen-lift boundaries | O(n) single-pass build, O(1) amortized push |
| `E: Array<number>` (valid time indices) | Time-tick candidate set | O(n) build, O(target) sample |

### Edge Case Handling

| Edge case | Handling | Which AC / test |
|-----------|----------|-----------------|
| `values.length === 0` | Step 1 → `state: 'no-samples'`, `emptyMessage: 'no readings recorded yet'` | AC-ERROR-1 |
| All entries `null` | Step 2 → `state: 'no-finite-values'`, `emptyMessage: 'no data to plot — sensor offline'`, `domain: null`. **No fabricated 0–0 or 0–max** | AC-HAPPY-2, AC-ERROR-2 |
| Single finite value among nulls | Step 3 pads ±5% (±1 at zero); value lands strictly inside `domain`; `segments` holds one 1-point run | Task file "single-value series" |
| Zero-range series (all finite values identical) | Identical to the above — one code path. Never `min === max`, never `range \|\| 1` | Task file "zero-range series" |
| Mixed null / finite | `finiteRange` skips nulls (step 2); step 9 closes the run at each null. No interpolated point, no interpolated tick | AC-HAPPY-1 |
| `Infinity` / `NaN` / non-number | `typeof v !== 'number' \|\| !isFinite(v)` — the *same* `finiteRange` gate used for min/max and for segments | R4 |
| Cross-metric contamination | Impossible: the function takes one array | AC-HAPPY-1 |
| `t.length === 0` | `state: 'no-samples'` | AC-ERROR-1 |
| All `time_valid: false` | `E` is empty → `state: 'clock-not-synced'`, `ticks: []`. **Zero** epoch-derived labels | AC-ERROR-3 |
| Mixed `time_valid` | `E` holds only valid indices; every tick's `index` is in `E`; caption discloses the gap | AC-ERROR-3 |
| Exactly one valid timestamp | `E.length (1) <= target` → `chosen = E` → one tick; `caption` is `first – last` with both the same instant | Edge #10 |
| `n === 1` | `sampleX(0, 1) = 0 / max(0, 1) = 0`; no division by zero | matches `app.js:143` |
| A tick rounding to `−0` | `formatTickLabel` step 1 reassigns `+0` | Edge #11 |

### Error Handling

There are no thrown errors and no logging (`console.*` is a blocking violation in `src/web/*.js`).
Every failure mode is a returned `state`.

| Condition | Response |
|-----------|----------|
| `values` is `null` / `undefined` | Treated as length 0 → `'no-samples'` (matches `finiteRange`'s existing tolerance, `dashboard-logic.js:120-122`) |
| `series` is `null` / `undefined` in `buildTimeAxis` | `'no-samples'` |
| `series.timeValid` shorter than `series.t` | Missing entries are falsy → those indices are simply not eligible. Degrades to fewer ticks, never to a bad label |
| `options` omitted | Defaults applied (`unit: ''`, target tick counts from module constants) |
| Malformed payload (strings where numbers expected) | Filtered by the `isFinite` gate; worst case an empty state, never a thrown exception that would blank the dashboard |

---

## R4 Mitigation — one function, no re-derivation

**The risk** (`dashboard-logic.js:113-118`, plan risk R4): `finiteRange`'s `isFinite` predicate and
`plotSeries`'s must agree, or the empty-state check and the renderer disagree about whether there is
anything to draw. Splitting one chart into three multiplies that risk by three.

**The single function both the predicate and the renderer must call: `buildMetricAxis(values, options)`.**

It returns **`state` and `segments` in the same object**. That is the whole mitigation, and it is
structural rather than advisory:

1. **You cannot obtain the predicate without also obtaining the geometry.** There is no exported
   boolean predicate for a single metric. The only way `app.js` learns whether a metric is plottable
   is by calling `buildMetricAxis`, and that call hands back the segments already computed — so
   there is no incentive, and no need, to write a second `isFinite` loop.
2. **`finiteRange` stays private** (it is already absent from the exports object,
   `dashboard-logic.js:220-228`). `app.js` cannot reach the raw predicate at all.
3. **`app.js` contains zero numeric predicates after this change.** `plotSeries`'s
   `values.filter(isFinite)`, its `Math.min`/`Math.max`, its `range = max − min || 1`, and its
   `v === null || v === undefined || !isFinite(v)` gap check (`app.js:126-148`) are all **deleted**.
   The renderer becomes:
   ```js
   var axis = DashboardLogic.buildMetricAxis(series.temp_c, { title: 'Water temperature', unit: '°C' });
   if (axis.state !== 'ok') { drawEmpty(axis.emptyMessage); return; }
   drawAxis(axis.ticks, axis.tickLabels, axis.tickPositions, axis.axisTitle);
   axis.segments.forEach(drawRun);
   ```
   Nothing in that snippet inspects a value. Any future reintroduction of `isFinite` into `app.js`
   is therefore a visible, reviewable regression rather than a quiet drift.
4. **`tickPositions` and `segments[].x/.y` are pre-computed on purpose.** Emitting `domain` alone
   would invite the renderer to write `(v − domain.min) / (domain.max − domain.min)` itself in three
   places with three chances to flip the axis direction. Shipping the fractions makes the
   convention (`y = 0` at `domain.min`) a single tested fact.
5. **`sampleX` is exported for the third renderer.** The water-level chart (WI-003) has no
   `MetricAxis` of its own, so it would otherwise hand-roll `i / (n − 1)`. Exporting the two-line
   mapping keeps all three charts and the time axis on one X coordinate system.

**Review rule to record in the build phase**: `grep -n "isFinite\|Math.min\|Math.max" src/web/app.js`
must return nothing after WI-004 lands.

---

## Label-Formatting Contract

### Numeric Y axes

| Aspect | Contract | Example |
|--------|----------|---------|
| Decimal places | **Derived from the step**, not from the unit: `decimals = clamp(−floor(log10(step)), 0, 6)`. Uniform down a single axis | step 0.5 → 1 decimal; step 2 → 0; step 0.05 → 2 |
| °C, typical bench range 20.1–21.9 | step 0.5, domain 20–22 | `'20.0' '20.5' '21.0' '21.5' '22.0'` |
| °C, wide range 4–38 | step 10, domain 0–40 | `'0' '10' '20' '30' '40'` |
| lux, 300–12000 | step 5000, domain 0–15000 | `'0' '5000' '10000' '15000'` |
| **lux in the thousands** | **Plain decimal integers. No thousands separator, no `k`/`K` suffix, no SI prefix, no `toLocaleString`.** BH1750's ceiling is 5 digits, which fits; locale-dependent output would be untestable without locale assertions | `65535` → axis `'0' '20000' '40000' '60000' '80000'` |
| Unit placement | **Not repeated on each tick.** Ticks are bare numbers; the unit appears once, in `axisTitle` (`'Water temperature (°C)'`, `'Ambient light (lux)'`), which the renderer draws once per chart. `unit` is also exposed separately so the UI/UX agent can place it independently | satisfies AC-HAPPY-1/2's "states °C" / "lux" |
| Negative zero | Never emitted; `−0` is normalized to `+0` before `toFixed` | `'0.0'`, never `'-0.0'` |
| Float noise | Impossible: ticks are snapped to the integer step grid before formatting | never `'0.30000000000000004'` |
| Non-`ok` states | `tickLabels: []`; the renderer draws `emptyMessage` instead | — |

### Time X axis

| Aspect | Contract | Example |
|--------|----------|---------|
| Tick label text | `formatAxisTimestamp(epochSec, true)` = `new Date(epochSec * 1000).toLocaleTimeString()` — **time of day only**, no date. Locale-formatted by the browser (never hand-rolled) | `'10:13:20 PM'` (en-US), `'22:13:20'` (en-GB) |
| Date context | Carried once in `caption`, using the full `formatReadingTimestamp` for the first and last **valid** samples | `'11/14/2023, 10:13:20 PM – 11/14/2023, 11:42:50 PM'` |
| Partially-unsynced window | `caption` appends `' (earlier samples: clock not synced)'`; the unsynced span simply carries no ticks | AC-ERROR-3 |
| **"Clock not synced" state** | `state: 'clock-not-synced'`, `ticks: []`, `validCount: 0`, `caption: 'clock not synced'` — mirroring `app.js:195`'s `'last updated (clock not synced)'`. **No near-1970 date is ever produced, because no tick is ever created from an invalid entry** | AC-ERROR-3 |
| Empty window | `state: 'no-samples'`, `caption: 'no readings recorded yet'` | AC-ERROR-1 |
| Test posture | Tests assert `typeof label === 'string'`, non-emptiness, and **which index** a tick came from — **never** the locale-rendered text, consistent with the existing `formatReadingTimestamp` tests | zero-dependency, locale-independent |

---

## Performance Expectations

### At Current Scale (180 samples × 3 metrics, every 30 s)

- Expected latency: **~60 µs** per full recompute (3 × 2 O(n) passes over 180 entries + ~20 tick
  computations + ~15 `toFixed` calls).
- Memory: ~8 KB transient per recompute (three `segments` arrays of ≤ 180 small objects), freed on
  the next poll.
- Throughput: ~16,000 recomputes/second available against a required 0.033/second. Headroom: ~5 orders
  of magnitude.
- Embedded code size added to the firmware image: **~1.4 KB uncompressed** across
  `niceStep`, `buildMetricAxis`, `buildTimeAxis`, `formatTickLabel`, `formatAxisTimestamp`, and
  `sampleX` (~95 SLOC), *offset* by ~35 SLOC deleted from `app.js`'s `plotSeries`. Net ≈ +1 KB
  against 16 MB flash at 30.4% used — **under 0.01%**.

### At 10× Scale (1,800 samples — a hypothetical `points=1800`)

- Expected latency: ~600 µs. Still imperceptible.
- Memory: ~80 KB transient.
- Tick counts are **unchanged** (they depend on the target count and the data's magnitude, not on n)
  — the axis does not degrade with window length.
- **The actual bottleneck at 10× is rendering, not this algorithm**: 1,800 points across a ~900 px
  panel is 2 points per pixel, so the renderer would want downsampling. That is a renderer concern
  and `reading_store_core_downsample()` already exists on the device side to serve it. This
  algorithm needs no change.

### Optimization Opportunities (deliberately not taken now)

1. **Tighter domain fit** — swap `niceStep`'s 1/2/5 thresholds for d3's √2 / √10 / √50 ladder. One
   line. Trades the provable `[3, 7]` tick bound for ~10% less wasted panel height. Take only if the
   bench complains about the lux axis overshoot.
2. **`options.nonNegative`** — clamp `domain.min` to 0 when `dataRange.min >= 0`, eliminating the
   `[−1, 1]` domain for a constant-zero lux series. ~3 lines + 1 test. Take only if a stuck-at-zero
   sensor is observed at the bench.
3. **Time-domain nice ticks** (round times: 10:15, 10:30) — ~50 SLOC and requires converting the
   plot's X mapping from index-proportional to time-proportional at the same time. Both must change
   together or the ticks will not line up with the data.
4. **Sticky domains** — hold the previous poll's domain unless the new data escapes it, to stop the
   axis rescaling every 30 s. ~10 lines, but requires state, which breaks the module's purity
   contract; it would have to live as an explicit `previousDomain` parameter.

---

## Validation Checklist

- [x] **Meets latency requirements** — ~60 µs against a 30 s recompute cadence; no stated latency SLA exists to violate.
- [x] **Meets memory requirements** — ~8 KB transient in the browser; ~1 KB net added to the firmware image (<0.01% of flash).
- [x] **Handles all edge cases** — all 11 named cases have a specified, testable behavior (§ Edge Case Handling).
- [x] **Scales to expected data size** — O(n) with n = 180 and a realistic 16× ceiling; tick counts are independent of n.
- [x] **Implementation feasible** — ~95 SLOC of plain ES5-style JavaScript in the existing UMD module, zero dependencies, no new files beyond the test suite.
- [x] **Respects Guiding Principles and data flow patterns in systemPatterns.md**:
  - *Pure-Logic / Device-Only Split* — every function is DOM-free, side-effect-free, host-testable under `node --test`; only drawing stays in `app.js`. The design **strengthens** the split by moving `plotSeries`'s min/max, gap, and normalization logic out of the device-only half.
  - *Configuration Is Not Hard-Coded* — one declared, bounded deviation for two presentation constants, both overridable per call (§ Guiding-Principle Deviation).
  - *Structured Logging* — no logging at all; failures are returned `state` values (satisfies the CLAUDE.md `console.*` block).
  - *Fail-Safe Defaults* — every malformed-input path degrades to an honest empty state; nothing throws, so a bad payload can never blank the dashboard.
  - *Cross-half precondition rule* — `buildTimeAxis`'s even-sample-spacing assumption is stated as an explicit precondition naming `src/sampler.c` as the responsible device-only module.
- [x] **File extension conventions** — `src/web/*.js`, `test/web/*.mjs` (systemPatterns.md § File Extension by Directory / Role).

---

## Testing Strategy

New suite: **`test/web/chart-axes.test.mjs`** (auto-discovered by `node --test test/web/*.test.mjs`).
Existing `test/web/dashboard-logic.test.mjs` stays at 25 pass / 0 fail, untouched.

### Unit Tests — numeric axis (WI-001)

1. `buildMetricAxis(fixture.temp_c, …)` returns the same domain/ticks whether or not the same
   fixture's `lux` array contains an absurd out-of-range value — **AC-HAPPY-1** (regression guard on
   the single-array signature).
2. A `null` entry in `temp_c` is excluded from `dataRange.min`/`.max` and appears in no tick — **AC-HAPPY-1**.
3. A **fully-null** `lux` array returns `state: 'no-finite-values'`, `domain === null`, `ticks` empty
   — **no fabricated 0–0 or 0–max** — **AC-HAPPY-2**.
4. `values: []` returns `state: 'no-samples'` with an `emptyMessage` **different** from case 3's —
   **AC-ERROR-1 vs AC-ERROR-2 must not collapse**.
5. Single finite value among nulls: `ticks.length >= 3` and `domain.min < value < domain.max`.
6. Zero-range series (all 180 samples `21.5`): `domain.min < 21.5 < domain.max`; no `NaN`, no
   `Infinity`, no `min === max`.
7. **Property**: across a table of ~10 ranges spanning 1e-3 to 1e5, `3 <= ticks.length <= 7` for
   `targetTickCount = 5`.
8. **Property**: `domain.min <= dataRange.min && domain.max >= dataRange.max` for the same table.
9. Ticks are exactly step-spaced and float-noise-free — assert `deepEqual(ticks, [20, 20.5, 21, 21.5, 22])`
   with `assert.deepStrictEqual`, which fails on `0.30000000000000004`-class values.
10. `tickLabels` all carry the same decimal count, derived from the step (`'20.0'` not `'20'`), and a
    lux axis in the thousands renders as plain integers (`'20000'`, no separator, no `k`).
11. No label is `'-0'` or `'-0.0'` for a domain straddling zero.
12. `segments`: a mid-series `null` produces exactly two segments; leading and trailing nulls produce
    no empty segment; an isolated finite value produces a 1-point segment.
13. `y` convention: the sample equal to `dataRange.min` has `y > 0` (padded domain) and
    `tickPositions[0] === 0`, last `=== 1`.

### Unit Tests — time axis (WI-002)

14. All-`time_valid: false` fixture → `state: 'clock-not-synced'`, `ticks.length === 0`,
    `caption === 'clock not synced'`. **Assert no tick object exists at all** — **AC-ERROR-3**.
15. Mixed fixture → every `tick.index` satisfies `series.timeValid[tick.index] === true` — **AC-ERROR-3**.
16. Empty window (`t: []`) → `state: 'no-samples'`, `ticks: []`, no throw.
17. `ticks.length <= targetTickCount`, and the first and last **valid** indices are both present.
18. `ticks[].x === index / (sampleCount − 1)` — the shared X mapping, asserted against `sampleX`.
19. A valid-suffix window: `caption` ends with `'(earlier samples: clock not synced)'`.
20. Labels are asserted as **non-empty strings only** — never against locale-rendered text.

### Regression

21. `formatReadingTimestamp`'s three existing tests remain green after the `formatEpoch` refactor.
22. `buildChartSeries`'s three existing tests remain green after the `t`/`timeValid` additions.

### Performance Tests

None. At n = 180 with a 30 s cadence there is nothing to benchmark that would not be measuring
`node --test`'s own overhead. The plan's regression floor (`node --test test/web/*.test.mjs`,
25 pass → ~45 pass) is the only gate.

### Explicitly NOT tested

Canvas/SVG pixel output (no browser harness), DOM wiring in `app.js`, the `/api/history` payload
shape (`test/test_reading_json/`), and `formatReadingTimestamp`'s locale output (already covered).

---

## Next Steps

1. **WI-001** — add `niceStep`, `formatTickLabel`, `buildMetricAxis`, and `sampleX` to
   `src/web/dashboard-logic.js`; export `buildMetricAxis` and `sampleX` (keep `niceStep` and
   `formatTickLabel` private unless a test needs them directly — prefer asserting through
   `buildMetricAxis`). Write `test/web/chart-axes.test.mjs` tests 1–13 RED-first.
2. **WI-002** — add the `formatEpoch` refactor, `formatAxisTimestamp`, the `t`/`timeValid`
   pass-through in `buildChartSeries`, and `buildTimeAxis`. Tests 14–22.
3. **WI-004** — rewrite `app.js`'s chart rendering against `axis.state` / `axis.emptyMessage` /
   `axis.segments`; delete `plotSeries`'s min/max, gap, and normalization logic. Verify
   `grep -n "isFinite\|Math.min\|Math.max" src/web/app.js` returns nothing.
4. Update `buildChartAriaLabel` to speak per-metric (it can keep using the private `finiteRange`, or
   consume `axis.dataRange` — either is consistent; prefer `axis.dataRange` so the aria text and the
   drawn chart cannot disagree).
5. Confirm the concurrent UI/UX decision (canvas vs SVG, panel geometry) consumes `tickPositions`,
   `segments[].x/.y`, and `sampleX` unchanged; if its layout wants a different tick density, it
   passes `options.targetTickCount` rather than changing this algorithm.

---

NEW_TERMS_INTRODUCED:
- **MetricAxis** — the return shape of `buildMetricAxis`: a per-metric chart model carrying `state`, nice-ed `domain`, `ticks`/`tickLabels`/`tickPositions`, and pre-split plot `segments`. The single object both the empty-state branch and the renderer consume.
- **TimeAxis** — the return shape of `buildTimeAxis`: `state`, index-anchored `ticks`, and an always-present honest `caption`.
- **nice-ed domain** — a data extent expanded outward to the nearest multiples of a 1/2/5×10^k step, so every tick is a round number.
- **segment** — a maximal contiguous run of finite samples in a series. Null/NaN/Infinity entries are segment boundaries, which is how "nulls are gaps, never zeros" is enforced in pure code.
- **`sampleX`** — the shared index→X-fraction mapping (`index / max(n−1, 1)`) used by all three charts and the time axis.
