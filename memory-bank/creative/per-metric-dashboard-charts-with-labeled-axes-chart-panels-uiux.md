# UI/UX Decision: Per-Metric Dashboard Chart Panels

**Created**: 2026-08-23
**Status**: DECIDED
**Decision Type**: UI/UX

## User Context

### Target Users
- **Primary**: "User (me)" — sole operator/owner of the hydroponic system (`productBrief.md` § Key Personas → Primary Users). Loads the device's LAN root URL on a phone or laptop, no login, no navigation. Wants to glance at the page and know how the system is doing; occasionally wants to read off an actual value or spot when something went wrong overnight.
- **Secondary**: none — single-user device, no admin role, no multi-tenant concerns.

### User Goals
1. Read a specific numeric value off a chart (e.g., "what was the water temperature around 3am?") — not just recognize a rising/falling shape.
2. Tell whether the water level dipped to LOW or hit FAULT overnight, and when.
3. Immediately distinguish "no data was ever recorded" from "sensor is currently offline" from "the clock isn't synced yet" — three different problems that today all look like the same blank canvas.

### Use Cases
| Use Case | User | Goal | Frequency |
|----------|------|------|-----------|
| Morning check | Owner | Did the water level drop overnight? Any FAULT episodes? | Daily |
| Bench debugging | Owner (also the developer) | Is the temp sensor actually reporting, or has it been offline for hours? | During firmware bring-up / troubleshooting |
| Phone glance | Owner | Quick read of current + recent trend before leaving the house | Ad hoc, mobile |
| Fresh device | Owner | First boot, before SNTP sync and before 24h of history exists | Once per flash/reset |

### Constraints
- **Devices**: LAN-only, phone (narrow viewport, no dev tools) and desktop browser. No app store, no PWA install flow assumed.
- **Accessibility**: Text-first — state must never be color-only (already enforced for badges in `style.css`/`deriveLevelBadge`). Screen-reader users get zero information from canvas pixels; per-chart accessible summaries are load-bearing, not decorative.
- **Existing Patterns**: Pure-Logic / Device-Only Split (`systemPatterns.md` § Code Organization Patterns) — axis math, tick generation, band segmentation, and text summaries are pure and Node-testable in `dashboard-logic.js`; only `<canvas>`/DOM drawing lives in `app.js`. Card section styling (`.card`, `.badge-*`) and CSS custom properties (`--level-*`) are established and should be reused, not reinvented.

## User Flow

### Flow Diagram
```
[GET / on LAN] → [Cards render current values] → [Three chart panels render below cards]
                                                          ↓
                                    [fetchHistory() success] → [all 3 charts redraw in place]
                                                          ↓
                                    [fetchHistory() failure] → [charts keep last-known state; poll-status shows failure]
```

### Flow Description
1. **Entry**: Owner opens the LAN root URL. No auth, no menu.
2. **Step 1**: Page loads with pre-script fallback text visible, then `dashboard-logic.js` and `app.js` load and the three current-value cards populate.
3. **Step 2**: Three chart panels render below the cards — temperature, light, water level — each independently scaled and headed.
4. **Decision Point (implicit)**: Owner visually scans for anything abnormal — a FAULT band, an offline chart, an unusually low temperature reading at a specific time.
5. **Exit**: No explicit exit — page is left open or closed; polling continues at 30s cadence while open.

### Error States
| State | Cause | User-visible signal |
|-------|-------|---------------------|
| No readings recorded yet | `n === 0`, fresh device/reset | Per-chart message: "no readings recorded yet" |
| Sensor offline, has history | All-null metric array, `n > 0` | Per-chart message: "no data to plot — sensors offline" |
| Clock not synced | `time_valid: false` on some/all entries | X axis omits invalid-timestamp ticks; if none are valid, an explicit "clock not synced" state replaces the time axis |
| History fetch fails | Network blip | All three charts keep last-known render; only `poll-status` line reflects the failure (unchanged from today) |

## Options Explored

### Option 1: Three inline SVG panels, DOM-native axes and labels
- **Approach**: Each metric gets an `<svg>` element built by `app.js` (still delegating scale/tick/segment math to `dashboard-logic.js`). Axis labels, tick text, and the level bands are real SVG `<text>`/`<rect>`/`<line>` elements in the DOM.
- **User Flow**: Same as above; rendering differs only in implementation.
- **Key UI Elements**: `<svg>` per panel, `<text>` tick labels, `<line>` gridlines, `<path>` for temp/light traces, `<rect>` bands for level.
- **Pros**:
  - Axis labels and tick text are natively in the accessibility tree — no separate text-summary construction needed for *labels*, only for a spoken value-range summary.
  - Crisp at any zoom/DPI, easy CSS theming via `currentColor`/custom properties.
  - Text elements are literally selectable/searchable (a bench debugging nicety).
- **Cons**:
  - Departs from the established canvas pattern used for the current history chart — introduces a second on-page rendering technology with no precedent in this codebase.
  - Materially more markup generated per redraw (many `<text>`/`<rect>`/`<path>` nodes × 3 panels × every poll) — this is DOM churn on every 30s poll, and the DOM construction logic itself is new device-only surface in `app.js` beyond what a canvas draw loop needs.
  - Does not eliminate AC-HAPPY-4's per-chart accessible *value-range* summary requirement (an axis with tick marks still doesn't say "8.2 to 24.6 °C" as one sentence) — so WI-004's pure summary-building logic survives regardless, just narrower in scope.
  - No line-count/asset-size precedent to compare against; harder to bound the flash-image cost up front since it's proportional to DOM complexity, not a fixed drawing routine.
- **Usability**: High — ticks and units are unambiguous once drawn.
- **Accessibility**: High for tick/label text; still needs the same offline/range narrative summary as canvas.
- **Implementation Complexity**: Medium-High — new SVG-building code path in `app.js`, plus the same pure logic Option 2 needs.

### Option 2: Three `<canvas>` elements, one per metric, with axis-drawing added to the existing draw routine
- **Approach**: Replace the single 900×320 `<canvas id="history-chart">` with three smaller canvases, one per metric, each with its own `role="img"` + `aria-label`. `app.js` gains three thin draw functions (temp line, light line, level band-strip) that all call into new pure helpers in `dashboard-logic.js` for: axis tick values, tick label formatting, per-series finite range, level-band segmentation, and per-chart accessible summary text.
- **User Flow**: Same as above.
- **Key UI Elements**: `<canvas id="chart-temp">`, `<canvas id="chart-light">`, `<canvas id="chart-level">`, each in its own `<section>`/panel with an `<h2>` heading and its own empty-state text drawn onto the canvas (mirroring today's `drawFrame`/empty-state pattern).
- **Pros**:
  - Directly extends the established, already-shipped, already-tested pattern (canvas + pure-logic split) — zero new rendering technology, zero new firmware-asset risk beyond what's already accepted.
  - Fixed, bounded draw-routine complexity per redraw (same cost profile as today's single chart, just triplicated) — easy to reason about flash/CPU cost.
  - Every accessibility requirement (AC-HAPPY-4, AC-ERROR-1/2/3) is already solved in miniature by the existing `buildChartAriaLabel` — extending it to three per-chart summaries is additive, not a new invention.
  - Keeps WI-004 scoped exactly as the plan anticipated: three summary builders + three empty-state selectors, straightforward Node-testable pure functions.
- **Cons**:
  - Axis tick labels must still be drawn as canvas text (no free DOM accessibility) — so the *only* representation of "24.6 °C" for assistive tech is the aria-label summary sentence, not a literal per-tick label. This is an accepted trade, already true of today's shipped chart.
  - Three canvases means three `role="img"`/`aria-label` pairs to keep in sync on every redraw — mechanical but must not be dropped.
- **Usability**: High — sighted users get real ticks with numbers drawn on-canvas; each panel is self-contained and independently scaled.
- **Accessibility**: High, via three focused `aria-label` summaries (each shorter and more specific than today's single combined one) rather than free DOM labels.
- **Implementation Complexity**: Medium — mostly extending existing, proven code paths.

### Option 3: One canvas, three internally-divided sub-regions (stacked bands within a single `<canvas>`)
- **Approach**: Keep a single `<canvas>` element (same DOM/asset footprint as today) but partition its drawing area into three horizontal bands, each independently scaled, with tick labels and heading text drawn inside each band.
- **User Flow**: Same visual outcome as Option 2 (three panels) but rendered as regions of one canvas rather than three canvas elements.
- **Pros**:
  - No new DOM elements, minimal HTML/CSS delta from today.
  - Reflow is trivial to reason about because there's exactly one element with one aspect ratio to manage.
- **Cons**:
  - **Fails AC-ENTRY-1's spirit and the invocation-method requirement that each metric be independently identifiable by heading text, not position alone** — a single canvas has one `role="img"`/one accessible name unless three separate `aria-label`-bearing regions are faked via ARIA tricks canvas doesn't support well (no standard "canvas sub-region" accessibility primitive exists). Screen readers would see one image with one label, undermining the whole point of "per-chart" from an a11y perspective.
  - Mobile reflow is much harder: a single fixed-aspect-ratio backing store split into three internal thirds either wastes space at phone width or forces a very tall single canvas — a genuinely worse responsive story than three independently-stacking block elements.
  - Any future incremental change (e.g. wanting the level chart to be more compact than the two line charts) requires re-deriving internal pixel offsets by hand instead of ordinary CSS layout.
- **Usability**: Medium — visually plausible but structurally fights the "three distinct panels" requirement.
- **Accessibility**: Low — one accessible name for three metrics is a regression risk against AC-ENTRY-1 and AC-HAPPY-4's "each chart's accessible summary" language, which presumes three summaries reachable independently.
- **Implementation Complexity**: Medium, but for the wrong reasons (hand-rolled internal layout math instead of using the browser's box model).

## Evaluation Matrix

| Criteria | Option 1 (SVG) | Option 2 (3 canvases) | Option 3 (1 canvas, 3 regions) |
|----------|----------|----------|----------|
| Usability | High | High | Medium |
| Accessibility | High (labels free, summary still needed) | High (via 3 summaries) | Low (1 accessible name for 3 metrics) |
| Consistency with existing pattern | Low (new tech) | High (extends shipped pattern) | Medium (same element, different structure) |
| Responsiveness | High | High (each panel reflows independently) | Low (single fixed-AR backing store) |
| Performance / asset size (flash impact) | Uncertain, DOM-proportional | Bounded, matches today's cost profile ×3 | Bounded, but layout math cost |
| Implementation Effort | Medium-High | Medium | Medium (fights the requirement) |

## Decision

**Chosen**: Option 2 — Three `<canvas>` elements, one per metric.

### Rationale
This is the R1 "one-way door" decision and it resolves in favor of canvas, for reasons specific to *this* codebase rather than a generic canvas-vs-SVG preference:

1. **It is the established, shipped, already-tested pattern.** `systemPatterns.md` explicitly documents the Pure-Logic/Device-Only Split's "first extension... to the browser layer" as canvas + `dashboard-logic.js`. Introducing SVG here would be a second, parallel rendering technology on a page that has exactly one precedent, for a task whose acceptance criteria do not require DOM-native tick labels — they require *numeric* tick labels (satisfiable by canvas text) and an accessible summary (already the existing pattern's job).
2. **Bench-verifiable, bounded cost (R3, R5).** Three canvases with fixed dimensions and a fixed per-redraw draw routine have a cost profile a human can reason about by inspection — it's the same `drawFrame`/`plotSeries` shape, tripled, with axis math added. SVG's DOM node count scales with tick count × series length × redraw frequency, which is harder to bound ahead of a flash-size check and introduces a new kind of "silent flash growth" risk (R5) via markup weight rather than a fixed binary asset.
3. **AC-ENTRY-1's "identifiable by heading text, not position alone" is satisfied cleanly**: each canvas sits in its own `<section>` with its own `<h2>`, own `role="img"`, own `aria-label`. This is strictly better on this axis than Option 3 and equal to Option 1.
4. **It does not eliminate accessible-summary work — it does not need to.** AC-HAPPY-4 asks for a summary stating unit, range, and explicit offline status, which is a sentence, not a set of DOM tick labels. That work is already proven out in `buildChartAriaLabel`; extending it to three shorter, per-metric versions is squarely inside the plan's WI-004 scope, not new invention.

### Trade-offs Accepted
- **No free DOM accessibility for individual tick values.** A screen-reader user gets "8.2 to 24.6 °C over 180 samples" as one sentence, not a walkable list of 6 individual tick labels. This mirrors today's shipped behavior and is an accepted, already-precedented trade for this project — mitigated by making the three new summaries more specific (per-metric) than today's single combined one.
- **Canvas text rendering is slightly more code than SVG `<text>`** (manual x/y positioning, `ctx.textAlign`/`ctx.textBaseline` bookkeeping per tick) — accepted because it's the same idiom `drawFrame`'s empty-state text already uses; no new technique to learn or maintain.

## Design Specifications

### Layout

Three stacked `<section class="chart-panel">` blocks replace the single `<section class="chart-section">` (`index.html:36-48`), in this order: temperature, light, water level. Order matches the existing card order (`#card-temp`, `#card-light`, `#card-level`) so the visual grouping stays predictable top-to-bottom.

- **Desktop (> 1024px)**: three panels stacked vertically, full content width (matches today's single-chart-section width; no side-by-side layout — three side-by-side charts at this width would each be too narrow for legible tick labels, and stacking is simpler to reason about for reflow at all sizes).
- **Tablet (640–1024px)**: same vertical stack, canvases scale down via existing `width:100%; height:auto` pattern.
- **Mobile (< 640px)**: same vertical stack; canvas backing store width is reduced (see below) so tick labels don't crowd out the plot area at narrow physical pixel widths.

Each panel is visually consistent with the existing `.card`/`.chart-section` styling (white card background, border, radius) so the page doesn't introduce a new visual language.

### Key Components
| Component | Purpose | Behavior |
|-----------|---------|----------|
| `<section class="chart-panel" id="panel-temp">` | Temperature chart container | Contains `<h2>Water Temperature (°C)</h2>`, `<canvas id="chart-temp">`, empty-state text drawn on-canvas |
| `<section class="chart-panel" id="panel-light">` | Light chart container | `<h2>Ambient Light (lux)</h2>`, `<canvas id="chart-light">` |
| `<section class="chart-panel" id="panel-level">` | Level chart container | `<h2>Water Level</h2>`, `<canvas id="chart-level">` — no unit in heading; level is categorical, unit-less by nature |
| `<p class="poll-status" id="poll-status">` | Shared fetch-status line | **Unchanged, singular** — stays below all three panels per AC-ASYNC-1/2 (one shared fetch path, one status line for the whole history refresh, not per-chart) |

Exact heading text (binding, matches invocation method spec verbatim): `Water Temperature (°C)`, `Ambient Light (lux)`, `Water Level`.

### Canvas dimensions
- Backing store: `width="900" height="220"` for temp and light (shorter than today's 320 since each chart now only needs one series, not two overlaid); `width="900" height="140"` for the level chart (a categorical band strip needs less vertical room than a continuous line plot — reserve ~40px for tick labels at the bottom, ~30px top margin for the band, remainder for FAULT-highlight breathing room per the encoding below).
- These are presentation literals (chart pixel dimensions), explicitly covered by the plan's stated "Configuration Is Not Hard-Coded" exemption for presentation constants in `src/web/*` — precedented by today's `width="900" height="320"`.
- CSS: extend the existing `#history-chart` rule (`style.css:121-126`) into a shared class:
  ```css
  .chart-panel canvas {
    width: 100%;
    max-width: 100%;
    height: auto;
    display: block;
  }
  ```
  Applies uniformly to all three canvases via a shared selector rather than three ID-specific rules — each canvas keeps its own `id` for `getContext` lookup and its own aspect ratio via its own `width`/`height` attributes, but shares layout CSS.

### Interactions
| Trigger | Action | Feedback |
|---------|--------|----------|
| Page load | `fetchHistory()` runs once (unchanged call site, `app.js:209`) | All three charts render from one `historyPayload` |
| 30s poll tick | `fetchHistory()` re-runs (unchanged, `app.js:204-207`) | All three charts redraw in place from the new payload — no flicker beyond a normal `clearRect`+redraw |
| Fetch failure | `.catch` in `fetchHistory` (unchanged, `app.js:176-181`) | `poll-status` line shows failure text; all three canvases keep their last successfully-drawn frame (AC-ASYNC-2) |
| Window resize / mobile rotation | CSS `width:100%; height:auto` scales each canvas | No JS redraw needed — browser scales the existing raster, consistent with today's behavior |

### Responsive Behavior
| Breakpoint | Changes |
|------------|---------|
| < 640px | Panels remain full-width stacked; canvas CSS scaling shrinks all three proportionally. Font size used for on-canvas tick/axis text stays a fixed px value drawn at the 900px backing-store resolution (so it scales down with the canvas, same as today's empty-state text at `14px`) — no separate low-DPI font logic needed. |
| 640–1024px | No structural change from mobile — same stack, same scaling. |
| > 1024px | Same stack; panels simply have more available width, tick labels get proportionally more breathing room. |

No layout breakpoint changes structure (single column throughout) — the task's own scope note ("no zoom/pan/new retention windows") argues against also introducing a side-by-side desktop layout that would need its own breakpoint-specific tick-density logic.

### Empty-State Copy (exact strings, per chart)

Each canvas, independently, on its own empty condition (mirrors today's `drawChart` empty-state branch, `app.js:103-121`, now evaluated three times with per-series inputs instead of once with combined inputs):

- **AC-ERROR-2** (`n === 0`, no history at all yet): `"no readings recorded yet"` — identical wording to today's shared message, now per-chart.
- **AC-ERROR-1** (`n > 0` but this metric's array is entirely null): `"no data to plot — sensors offline"` for temp/light. For the level chart, the equivalent all-`UNKNOWN` condition (level is never `null` per `level_json_name`, but can be all-`UNKNOWN` if the level switch never reported before its own store history began) uses the same string for consistency: `"no data to plot — sensors offline"`.
- **AC-ERROR-3** (clock not synced): if **zero** entries have `time_valid: true`, replace the X axis tick row with a single centered label: `"clock not synced"`, drawn where tick labels would normally sit, directly below the plot area — the plotted series (if any) still renders above it; only the time axis is degraded, not the whole chart. This exactly mirrors `app.js:195`'s existing `"last updated (clock not synced)"` phrasing, applied per-chart to the axis instead of to the global status line. If **some** entries have `time_valid: true` and some don't, ticks are drawn only from valid entries (per AC-HAPPY-1) — no "not synced" message needed since honest ticks exist.

### Water-Level Chart: Encoding Decision

**Form chosen: a single-row band/step strip** (not a step *line*, not a multi-lane heat strip) — one continuous horizontal strip across the full chart width, height ~60% of the level canvas's plot area, divided into contiguous colored+labeled segments, one segment per contiguous run of the same `level` value across the time axis.

- **Why not a step line**: a line implies an ordinal/numeric relationship between FULL/MID/LOW/FAULT (as if FAULT were "below LOW" on a continuous scale) that doesn't exist — FAULT is a sensor-fault condition, not a level below LOW. A band avoids implying false ordinality.
- **Why not a multi-lane heat strip**: only one boolean-ish state is active at a time (the switch reports one level per reading) — multiple lanes would misrepresent a single-valued signal as if it had independent per-lane truth values.
- **Why a band over a step line generally**: a filled, labeled band is easier to read at a glance for "was it LOW for a long stretch or a blip" than a thin stepped line, and gives more room for inline text labels than a 2px line does.

**Non-color FAULT distinction (binding for AC-HAPPY-3)**:
1. **Every segment carries an inline text label** drawn inside (or, if the segment is too narrow, immediately above) the band: `FULL`, `MID`, `LOW`, `FAULT`, or `UNKNOWN`. Text is the primary distinguisher, not fill color — consistent with the existing `deriveLevelBadge` pattern that already assigns each level its own text and CSS class.
2. **FAULT segments additionally get a distinct fill *pattern*, not just a distinct hue**: a diagonal-hatch fill (drawn via repeated short canvas line strokes at 45°, cheap to compute, no image asset) layered under the `FAULT` text label. This ensures FAULT is distinguishable even to a color-blind viewer or on a low-quality display where the fault-red and low-orange hues (`--level-fault: #b3261e` vs `--level-low: #b8420b` — close in hue) might otherwise be hard to tell apart at a glance. UNKNOWN segments get a light diagonal-hatch too (lower density) so pre-first-sample/gap segments are visually distinct from a confident FAULT reading — this directly satisfies "FAULT must never be visually or textually confusable with... UNKNOWN."
3. Colors reuse the existing CSS custom properties one-for-one so the level chart and the level badge agree visually: `--level-full`, `--level-mid`, `--level-low`, `--level-fault`, `--level-unknown` (already defined in `style.css:16-22`). Canvas can't read CSS custom properties directly from JS without a `getComputedStyle` call — acceptable and cheap, done once per draw, consistent with `plotSeries`'s existing hardcoded hex approach today (`app.js:160-161` already hardcodes hex rather than reading CSS vars, so either approach is precedented; reading the custom properties via `getComputedStyle(document.documentElement)` is preferred here specifically so the level *chart* and level *badge* can never visually drift apart from each other as one is themed and the other isn't).
4. Minimum segment width for an inline label: if a segment is narrower than roughly the width of its label text, the label is omitted from inside the band and the segment still gets its hatch/color treatment; the chart's accessible text summary (below) is what carries the information for very short-lived segments regardless — the visual is a supplementary read, not the sole source of truth (this mirrors the "text-first" accessibility posture already established for badges).

### Per-Chart Accessible Summaries (successor to `buildChartAriaLabel`)

Each canvas keeps `role="img"` and gets its own `aria-label`, refreshed on every draw exactly as today (`app.js:99`, extended ×3). Three new pure functions in `dashboard-logic.js` replace the one combined `buildChartAriaLabel`:

- `buildTempChartAriaLabel(series)` → e.g. `"Water temperature chart over 180 samples. 8.2 to 24.6 °C."` or, all-null: `"Water temperature chart over 180 samples. No plottable data. Water temperature is offline."` or, `n===0`: `"Water temperature chart. No readings recorded yet."`
- `buildLightChartAriaLabel(series)` → same shape, unit "lux", subject "Ambient light".
- `buildLevelChartAriaLabel(series)` → describes the *sequence of bands*, not a numeric range, e.g.: `"Water level chart over 180 samples. FULL for the first 40 samples, then MID, then LOW, then FAULT for the most recent 12 samples."` — collapsing consecutive identical readings into named runs (this is the pure segmentation logic WI-003 already needs to produce for drawing, reused here for the summary — one function, two consumers). All-`UNKNOWN`/`n===0` cases use the same phrasing pattern as the other two charts.
- Each of the three explicitly states "offline"/"no readings recorded yet" rather than omitting a chart's information — preserving the existing "never silently omit" property (`dashboard-logic.js:183-186`) per-chart instead of once combined.

### Accessibility Requirements
- [x] Keyboard navigation — no interactive controls on the charts (no zoom/pan per scope), so no new tab-stops are introduced; nothing to trap focus in.
- [x] Screen reader compatibility — three `role="img"` canvases, each with its own specific `aria-label`, refreshed every redraw.
- [x] Color contrast — reuse existing `--level-*`/`--live`/`--offline` custom properties, already presumed WCAG-checked for the badges; FAULT/UNKNOWN also get non-color (hatch pattern + text) differentiation per AC-HAPPY-3.
- [x] Focus indicators — n/a, no focusable elements added.
- [x] Error messages accessible — empty-state and clock-not-synced text is drawn as part of the same canvas whose `aria-label` also states the equivalent condition in words (so it's not canvas-text-only; it's doubly represented).

## R1 Consequence — Effect on WI-004 and Phase 2 Scope

**Choosing canvas (Option 2) does NOT shrink WI-004** the way choosing SVG might have. WI-004 ("per-chart summaries + empty-state selection") remains necessary in full because:

- The accessible summary is a **narrative sentence about range/offline/segment-sequence**, which no axis-tick markup — canvas or SVG — provides for free. SVG would only have removed the need to draw *tick label glyphs* in markup; it would not have removed the need to describe "8.2 to 24.6 °C" or "FAULT for the most recent 12 samples" as prose for assistive tech, because that's a *summary*, not a *label enumeration*.
- Phase 2's scope is therefore: (1) three pure summary-builder functions (`buildTempChartAriaLabel`, `buildLightChartAriaLabel`, `buildLevelChartAriaLabel`) replacing the one combined `buildChartAriaLabel`; (2) three pure empty-state-selector helpers (or one parameterized helper reused three times) replacing the single `n===0`/`hasPlottableData` branch; (3) the level-band segmentation function shared between drawing and the level summary (per WI-003's dependency on this doc's level-chart-form decision, now resolved as band/step strip).
- What canvas *does* narrow, relative to SVG, is the drawing-side surface in `app.js`: three bounded draw routines (line-plot ×2, band-strip ×1) each extending the existing `drawFrame`/`plotSeries` shape with tick-drawing added, rather than a new DOM-construction subsystem. This keeps Phase 2/WI-006 scoped to "extend proven code," not "build a new rendering layer."

## Implementation Guidelines

### For Developers
1. Do not delete `plotSeries`'s null-handling contract (`app.js:140-156`, "gap: lift the pen") — each of the three new per-metric plot routines must preserve it verbatim; a null gap must never interpolate or coerce to 0.
2. Compute each chart's tick values and tick label text in `dashboard-logic.js` (pure) and pass fully-formatted strings to `app.js`'s draw routines — `app.js` should never decide *what* a tick says, only *where* to paint the string it's given (same separation `deriveMetricBadge`/`deriveLevelBadge` already model for badges).
3. The level-band segmentation function (contiguous-run detection over the `level` array) must live in `dashboard-logic.js` and be shared verbatim between the level-chart draw routine and `buildLevelChartAriaLabel` — do not reimplement run-detection twice.
4. Read `--level-*` CSS custom properties via `getComputedStyle(document.documentElement).getPropertyValue(...)` once per level-chart redraw, rather than hardcoding hex a second time, so the level chart can never visually drift from the level badge.
5. Keep the existing `fetchHistory().then(...)` single success path (`app.js:172-175`) as the sole place all three charts get redrawn — do not introduce three separate fetches (AC-ASYNC-1 is explicit about this).
6. No `console.log`/`console.error` anywhere in the new `app.js` code — failures stay user-visible text via `poll-status`, per CLAUDE.md § Observability Standards.

### Component Structure
```
src/web/
├── index.html                 ← replace lines 36-48 (single chart-section) with three chart-panel sections
├── style.css                  ← replace #history-chart rule (121-126) with shared .chart-panel canvas rule; add .chart-panel section styling reusing .chart-section conventions
├── app.js                     ← replace drawChart/drawFrame/plotSeries (77-162) with three per-metric draw routines + shared axis-drawing helpers
└── dashboard-logic.js         ← add: buildTempChartAriaLabel, buildLightChartAriaLabel, buildLevelChartAriaLabel, computeAxisTicks (or per-metric variants), segmentLevelRuns, per-chart empty-state selector(s); keep formatReadingTimestamp, deriveMetricBadge, deriveLevelBadge, isPreFirstSample, buildChartSeries unchanged
```
No new files. All four embedded assets (`index.html`, `style.css`, `app.js`, `dashboard-logic.js`) are reused as-is — see R2 disposition below.

### Recommended Libraries/Patterns
- No library — continues the "no CDN, no chart library" constraint using raw Canvas 2D API, exactly as today.
- Reuse `getComputedStyle` for reading CSS custom properties (browser-native, zero dependency) rather than hardcoding a second color table.

## R2 Disposition — Asset Count

**Recommending the default: extend the existing four embedded assets. No 5th asset, no firmware-side change.**

All new logic (axis tick computation, level-band segmentation, three per-chart summary builders, three draw routines) fits inside the two existing JS files (`dashboard-logic.js` for pure logic, `app.js` for DOM/canvas work) and the two existing markup/style files (`index.html`, `style.css`). No new route, no new `_binary_*` symbol, no `embed_web_assets.py`/`http_api.h`/`http_api.c` change is needed. This avoids the full R2 cost (new entry in `WEB_ASSETS`, new `extern` declaration, new route table entry — all firmware changes explicitly out of this task's scope).

## Validation Checklist

- [x] Meets all user goals — per-metric readable values, level history now visible, offline/no-data/clock-not-synced states unambiguous
- [x] Accessible per requirements — three independent `role="img"`/`aria-label` pairs, text-first FAULT/UNKNOWN distinction, no color-only signaling
- [x] Consistent with existing patterns — extends canvas + Pure-Logic/Device-Only Split, reuses `.card`/`.badge`/CSS custom properties
- [x] Respects Guiding Principles and component architecture in systemPatterns.md — axis/tick/segmentation/summary logic is pure and Node-testable; only drawing lives in `app.js`; presentation literals (canvas pixel dims) fall under the documented exemption
- [x] Responsive across devices — single-column stack at all breakpoints, existing `width:100%; height:auto` scaling pattern retained
- [x] Performance acceptable — bounded per-chart draw cost, no new DOM churn, no new assets
- [x] Implementation feasible — no firmware change, no new asset, extends four files already embedded

## Next Steps

1. WI-003 (level segmentation) and WI-004 (per-chart summaries + empty-state selectors) can proceed in `dashboard-logic.js` per the exact function signatures/behaviors specified above.
2. WI-005 (markup + styling) can proceed against the exact heading text, element IDs, canvas dimensions, and CSS rule given above.
3. WI-006 (per-chart renderers in `app.js`) can proceed once WI-003/WI-004 land, since the draw routines consume their pure outputs directly.
4. Bench-verify per R3: load the page with (a) both sensors live, (b) both sensors unplugged with existing history, (c) a fresh/reset device with zero history, (d) before SNTP sync — confirming each of the four empty/degraded states renders its distinct copy.

---

NEW_TERMS_INTRODUCED:
- `chart-panel` (CSS class / section role) — one of the three per-metric chart containers (`#panel-temp`, `#panel-light`, `#panel-level`), replacing the single `chart-section`.
- Level "band/step strip" — the chosen water-level chart form: a single horizontal strip of contiguous, individually labeled and hatched-or-colored segments, one per run of unchanged `level` value over time.
