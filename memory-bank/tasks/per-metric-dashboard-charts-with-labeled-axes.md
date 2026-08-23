---
slug: per-metric-dashboard-charts-with-labeled-axes
legacy_id:
feature: per-metric-dashboard-charts-with-labeled-axes
status: INITIALIZED
---

# per-metric-dashboard-charts-with-labeled-axes: Per-metric Dashboard Charts With Labeled Axes

**Complexity**: Level 3
**Status**: INITIALIZED
**Roadmap**: per-metric-dashboard-charts-with-labeled-axes
**Branch**: feature/per-metric-dashboard-charts-with-labeled-axes
**Worktree**: N/A

## Task Description

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

### Scope

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

### Constraints inherited from the existing dashboard

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

### Out of scope

- Firmware, sensor driver, and reading-store changes. `/api/history` and `/api/now`
  already carry everything these charts need.
- New retention windows, zoom, pan, or range selection.
- Any change to the three current-value tiles or their badges.

## User Journey Definition

**Feature Type**: End-User Feature
**Creative Phase Required**: Yes — pending Spec Writer confidence assessment

### Invocation Method (End-User Features)
- **Location**: TBD by Spec Writer
- **Element**: TBD by Spec Writer
- **Visibility**: TBD by Spec Writer
- **Navigation**: TBD by Spec Writer

### Success Criteria (End-User Features)
- **User sees**: TBD by Spec Writer
- **User can verify at**: TBD by Spec Writer
- **Data persisted**: TBD by Spec Writer
- **Observable within**: TBD by Spec Writer

### Acceptance Criteria
TBD by Spec Writer

## Test Strategy

TBD during Step 5 (Create Implementation Plan)

## Implementation Roadmap

### New Source Files (pin path + extension)
TBD during Step 5

### Phases
TBD during Step 5

## Creative Phases

- [ ] Pending — set at Step 3.3

---

## Execution State

**Build Status**: RUNNING
**Current Phase**: PLAN
**Current Step**: Task auto-provisioned from roadmap feature (Step 0.1)
**Last Completed**: N/A
**Can Resume**: NO

### Active Sub-Agents
(none)

### Completed Steps
- Step 0.0: Resolved `per-metric-dashboard-charts-with-labeled-axes` as a roadmap feature with no linked task
- Step 0.1: Task file created; branch `feature/per-metric-dashboard-charts-with-labeled-axes` cut off `origin/main`
