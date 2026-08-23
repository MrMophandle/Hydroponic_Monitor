---
version: next
status: planned
priority: medium
complexity: 3
linked_tasks: []
created: 2026-08-23
---

# Per-metric Dashboard Charts With Labeled Axes

Replace the dashboard's single overlaid history canvas with one chart per metric, each
carrying labeled axes so a reading can be taken off the chart instead of merely
recognized as a shape.

Today `src/web/app.js` draws water temperature and ambient light as two lines on one
900x320 canvas, and `plotSeries` rescales each series independently to its own
min/max. The lines therefore share a plot area but not a scale: their relative heights
and every crossing point are artifacts of two unrelated normalizations. The canvas has
no tick marks, no numeric labels, no units, no time axis and no legend — `drawFrame`
draws a bare left edge and baseline only. Water level is carried in the `/api/history`
payload and survives `buildChartSeries`, but is never plotted; `hasPlottableData`
excludes it by design.

## Scope

Three charts, one per metric:

- **Water temperature** — line, Y axis in °C
- **Ambient light** — line, Y axis in lux
- **Water level** — categorical step/band trace over FULL / MID / LOW / FAULT

Each chart owns its own Y scale and states its unit, which removes the shared-plot-area
problem rather than papering over it. The X axis is time across the retained window,
labeled from the same history timestamps the tiles already use.

Charting water level is a net addition: the device has been storing a 24-hour level
history since the original dashboard shipped and has never shown it. A level trace is
what makes drain rate, refill events, and FAULT episodes legible over time, none of
which the current single-value badge can express.

## Constraints inherited from the existing dashboard

- **No CDN, no chart library.** The page is LAN-only with no internet path
  (`index.html` load-order comment). Whatever draws the axes ships in the firmware
  image.
- **Pure-Logic / Device-Only Split** (`systemPatterns.md`). Tick generation, scale
  selection, axis label formatting and per-chart text summaries belong in
  `dashboard-logic.js` under Node tests; only drawing and DOM work belong in `app.js`.
- **Nulls are gaps, never zeros.** `buildChartSeries` passes `null` through unchanged
  and `plotSeries` lifts the pen. Axis work must not reintroduce a false plunge to
  zero, and must not let an all-null series claim a numeric range.
- **The clock may be unsynced.** `formatReadingTimestamp` returns `null` before SNTP
  sync because this board has no battery-backed RTC. A time axis has to render
  honestly with no trustworthy timestamps rather than printing 1970.
- **Accessibility is text-first.** A canvas cannot expose pixels to assistive tech, so
  the single combined `aria-label` from `buildChartAriaLabel` has to become per-chart
  summaries without losing its explicit "offline" reporting — it currently refuses to
  silently omit a series it has no data for, and that property must survive.
- **Empty states stay explicit.** The distinct "no readings recorded yet" and "no data
  to plot — sensors offline" messages were added after a bench session where a blank
  canvas was indistinguishable from a failed render (2026-08-20). Each chart needs its
  own equivalent.
- **Asset count affects the firmware.** The four browser assets are embedded by
  `embed_web_assets.py` and served by hand-written handlers in `src/http_api.c` with
  `_binary_*` symbols. Splitting out a new JS or CSS file is not free — it touches the
  embed script and the C route table. Reusing the existing four files is not required
  but the cost of adding one is real.

## Open design questions for `/bmb:creative`

- Tick generation: interval selection ("nice" round steps), tick count, and behavior on
  a single-value series, a mostly-null series, and a series whose range is zero.
- Layout: three separate canvases versus stacked panels within one canvas, and how the
  set reflows on a phone. `#history-chart` is currently `width: 100%; height: auto` over
  a fixed 900x320 backing store.
- Level chart form: step line, filled bands, or a lane/heat strip — and how FAULT is
  distinguished from a legitimate band without relying on color alone.
- Canvas versus inline SVG. Canvas is the established pattern and needs an explicit text
  summary; SVG would put axis labels in the DOM for free at the cost of departing from
  the pattern and adding markup weight to an embedded asset.

## Out of scope

- Firmware, sensor driver, and reading-store changes. `/api/history` and `/api/now`
  already carry everything these charts need.
- New retention windows, zoom, pan, or range selection.
- Any change to the three current-value tiles or their badges.

**Complexity rationale**: Level 3 by the decision tree. Q1 no — this is a feature, not a
fix. Q2 no — it lands across `index.html`, `app.js`, `dashboard-logic.js`, `style.css`
and `test/web/dashboard-logic.test.mjs`, and the approach is not obvious. Q3 no — real
design decisions are required: the axis tick algorithm, the chart layout and its
responsive behavior, and the form of the categorical level trace. Q4 yes — those are
design decisions affecting multiple components of the web dashboard without being
system-wide. Q5 does not fire: scope is confined to `src/web/` and its host tests, with
no firmware or store changes and no phasing requirement, which is what separates this
from the Level 4 `sensor-monitoring-dashboard` work that built the dashboard originally.
