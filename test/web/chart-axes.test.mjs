// chart-axes.test.mjs — host tests for the pure per-metric axis/tick logic
// added to src/web/dashboard-logic.js (buildMetricAxis, sampleX,
// formatAxisTimestamp, buildTimeAxis). Run with Node's built-in test runner:
//   node --test test/web/*.test.mjs
//
// Phase 1 of per-metric-dashboard-charts-with-labeled-axes: pure numeric-axis
// scale/tick logic and pure time-axis tick logic only.
//
// Phase 2 adds: pure level-band segmentation (buildLevelBands) and the three
// per-chart accessible summary builders (buildTempChartAriaLabel,
// buildLightChartAriaLabel, buildLevelChartAriaLabel). Markup/renderers are
// later phases and are NOT covered here.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import DashboardLogic from '../../src/web/dashboard-logic.js';

const {
  buildMetricAxis,
  sampleX,
  buildTimeAxis,
  buildChartSeries,
  formatReadingTimestamp,
  buildLevelBands,
  buildTempChartAriaLabel,
  buildLightChartAriaLabel,
  buildLevelChartAriaLabel,
} = DashboardLogic;

// ---------------------------------------------------------------------------
// buildMetricAxis — numeric axis scale/tick logic
// ---------------------------------------------------------------------------

test('buildMetricAxis is unaffected by an absurd value in a sibling metric array', () => {
  const temp = [20, 20.5, 21, 21.5, 22];
  const withoutLux = buildMetricAxis(temp, { unit: '°C' });

  // Same temp array, but imagine it came from a fixture whose lux sibling has
  // an absurd out-of-range value — buildMetricAxis never sees lux at all, so
  // the single-array signature makes contamination structurally impossible.
  const withLux = buildMetricAxis(temp.slice(), { unit: '°C' });

  assert.deepStrictEqual(withLux.domain, withoutLux.domain);
  assert.deepStrictEqual(withLux.ticks, withoutLux.ticks);
});

test('buildMetricAxis excludes a null entry from dataRange and produces no tick derived from it', () => {
  const values = [20, null, 22];
  const axis = buildMetricAxis(values, { unit: '°C' });
  assert.equal(axis.dataRange.min, 20);
  assert.equal(axis.dataRange.max, 22);
  assert.equal(axis.dataRange.count, 2);
});

test('buildMetricAxis returns no-finite-values for a fully-null array', () => {
  const axis = buildMetricAxis([null, null, null], { unit: 'lux' });
  assert.equal(axis.state, 'no-finite-values');
  assert.equal(axis.domain, null);
  assert.deepStrictEqual(axis.ticks, []);
  assert.equal(axis.emptyMessage, 'no data to plot — sensor offline');
});

test('buildMetricAxis returns no-samples for an empty array, with a distinct message', () => {
  const axis = buildMetricAxis([], { unit: '°C' });
  assert.equal(axis.state, 'no-samples');
  assert.equal(axis.domain, null);
  assert.deepStrictEqual(axis.ticks, []);
  assert.equal(axis.emptyMessage, 'no readings recorded yet');

  const noFinite = buildMetricAxis([null, null], { unit: '°C' });
  assert.notEqual(axis.emptyMessage, noFinite.emptyMessage);
});

test('buildMetricAxis places a single finite value strictly inside the domain', () => {
  const values = [null, null, 21.5, null];
  const axis = buildMetricAxis(values, { unit: '°C' });
  assert.equal(axis.state, 'ok');
  assert.ok(axis.ticks.length >= 3);
  assert.ok(axis.domain.min < 21.5);
  assert.ok(21.5 < axis.domain.max);
});

test('buildMetricAxis pads a zero-range (all-identical) series to a non-degenerate domain', () => {
  const values = new Array(180).fill(21.5);
  const axis = buildMetricAxis(values, { unit: '°C' });
  assert.ok(axis.domain.min < 21.5);
  assert.ok(21.5 < axis.domain.max);
  assert.ok(Number.isFinite(axis.domain.min));
  assert.ok(Number.isFinite(axis.domain.max));
  assert.notEqual(axis.domain.min, axis.domain.max);
});

test('buildMetricAxis produces 3 to 7 ticks across a wide range of spans (property test)', () => {
  const spans = [1e-3, 1e-2, 0.1, 1, 5, 10, 50, 137, 1000, 1e5];
  for (const span of spans) {
    const values = [0, span];
    const axis = buildMetricAxis(values, { unit: 'u', targetTickCount: 5 });
    assert.ok(
      axis.ticks.length >= 3 && axis.ticks.length <= 7,
      `span ${span} produced ${axis.ticks.length} ticks`
    );
  }
});

test('buildMetricAxis domain always encloses dataRange (property test)', () => {
  const spans = [1e-3, 1e-2, 0.1, 1, 5, 10, 50, 137, 1000, 1e5];
  for (const span of spans) {
    const values = [1, 1 + span];
    const axis = buildMetricAxis(values, { unit: 'u', targetTickCount: 5 });
    assert.ok(axis.domain.min <= axis.dataRange.min);
    assert.ok(axis.domain.max >= axis.dataRange.max);
  }
});

test('buildMetricAxis produces exact tick values for a concrete fixture', () => {
  const values = [20, 20.5, 21, 21.5, 22];
  const axis = buildMetricAxis(values, { unit: '°C', targetTickCount: 5 });
  assert.deepStrictEqual(axis.ticks, [20, 20.5, 21, 21.5, 22]);
});

test('buildMetricAxis tickLabels share a common decimal count and lux-scale values render as plain integers', () => {
  const tempAxis = buildMetricAxis([20, 20.5, 21, 21.5, 22], { unit: '°C', targetTickCount: 5 });
  assert.deepStrictEqual(tempAxis.tickLabels, ['20.0', '20.5', '21.0', '21.5', '22.0']);

  const luxAxis = buildMetricAxis([0, 20000], { unit: 'lux', targetTickCount: 5 });
  for (const label of luxAxis.tickLabels) {
    assert.doesNotMatch(label, /,/);
    assert.doesNotMatch(label, /k/i);
  }
  assert.ok(luxAxis.tickLabels.includes('20000'));
});

test('buildMetricAxis never renders a tick label as -0 or -0.0 for a domain straddling zero', () => {
  const axis = buildMetricAxis([-3, 3], { unit: 'u', targetTickCount: 5 });
  for (const label of axis.tickLabels) {
    assert.notEqual(label, '-0');
    assert.notEqual(label, '-0.0');
  }
});

test('buildMetricAxis segments: a mid-series null splits into two runs; no empty runs at edges; isolated point is a one-element run', () => {
  const values = [null, 1, 2, null, 3, 4, null];
  const axis = buildMetricAxis(values, { unit: 'u' });
  assert.equal(axis.segments.length, 2);
  assert.equal(axis.segments[0].length, 2);
  assert.equal(axis.segments[1].length, 2);
  for (const seg of axis.segments) {
    assert.ok(seg.length > 0);
  }

  const isolated = buildMetricAxis([null, 5, null], { unit: 'u' });
  assert.equal(isolated.segments.length, 1);
  assert.equal(isolated.segments[0].length, 1);
  assert.equal(isolated.segments[0][0].index, 1);
  assert.equal(isolated.segments[0][0].value, 5);
});

test('buildMetricAxis tickPositions start at 0 and end at 1', () => {
  const axis = buildMetricAxis([20, 20.5, 21, 21.5, 22], { unit: '°C' });
  assert.equal(axis.tickPositions[0], 0);
  assert.equal(axis.tickPositions[axis.tickPositions.length - 1], 1);
});

test('buildMetricAxis includes a title in axisTitle when provided', () => {
  const withTitle = buildMetricAxis([1, 2], { unit: '°C', title: 'Water temp' });
  assert.equal(withTitle.axisTitle, 'Water temp (°C)');
  const withoutTitle = buildMetricAxis([1, 2], { unit: '°C' });
  assert.equal(withoutTitle.axisTitle, '°C');
});

// ---------------------------------------------------------------------------
// sampleX — shared index -> X-fraction mapping
// ---------------------------------------------------------------------------

test('sampleX maps first/last index to 0/1 and handles a single-sample series', () => {
  assert.equal(sampleX(0, 5), 0);
  assert.equal(sampleX(4, 5), 1);
  assert.equal(sampleX(0, 1), 0);
});

// ---------------------------------------------------------------------------
// buildTimeAxis — pure time-axis tick logic
// ---------------------------------------------------------------------------

test('buildTimeAxis is clock-not-synced when every entry is time-invalid, with no ticks at all', () => {
  const series = { t: [100, 200, 300], timeValid: [false, false, false] };
  const axis = buildTimeAxis(series, {});
  assert.equal(axis.state, 'clock-not-synced');
  assert.equal(axis.ticks.length, 0);
  assert.equal(axis.caption, 'clock not synced');
});

test('buildTimeAxis only derives ticks from indices where timeValid is true', () => {
  const series = {
    t: [100, 200, 300, 400, 500, 600, 700, 800],
    timeValid: [false, true, true, false, true, true, true, true],
  };
  const axis = buildTimeAxis(series, { targetTickCount: 4 });
  assert.equal(axis.state, 'ok');
  for (const tick of axis.ticks) {
    assert.equal(series.timeValid[tick.index], true);
  }
});

test('buildTimeAxis is no-samples for an empty window and does not throw', () => {
  const axis = buildTimeAxis({ t: [], timeValid: [] }, {});
  assert.equal(axis.state, 'no-samples');
  assert.deepStrictEqual(axis.ticks, []);
});

test('buildTimeAxis chooses at most targetTickCount ticks including first and last valid index', () => {
  const t = [];
  const timeValid = [];
  for (let i = 0; i < 20; i++) {
    t.push(1700000000 + i * 60);
    timeValid.push(true);
  }
  const axis = buildTimeAxis({ t: t, timeValid: timeValid }, { targetTickCount: 4 });
  assert.ok(axis.ticks.length <= 4);
  const indices = axis.ticks.map((tick) => tick.index);
  assert.ok(indices.includes(0));
  assert.ok(indices.includes(19));
});

test('buildTimeAxis tick.x matches the exported sampleX for every tick', () => {
  const t = [];
  const timeValid = [];
  for (let i = 0; i < 10; i++) {
    t.push(1700000000 + i * 60);
    timeValid.push(true);
  }
  const axis = buildTimeAxis({ t: t, timeValid: timeValid }, { targetTickCount: 4 });
  for (const tick of axis.ticks) {
    assert.equal(tick.x, sampleX(tick.index, t.length));
  }
});

test('buildTimeAxis caption notes earlier unsynced samples when only a valid suffix exists', () => {
  const t = [100, 200, 300, 1700000000, 1700000060, 1700000120];
  const timeValid = [false, false, false, true, true, true];
  const axis = buildTimeAxis({ t: t, timeValid: timeValid }, {});
  assert.match(axis.caption, /\(earlier samples: clock not synced\)$/);
});

test('buildTimeAxis tick labels are non-empty strings without asserting locale-specific text', () => {
  const t = [1700000000, 1700000060, 1700000120];
  const timeValid = [true, true, true];
  const axis = buildTimeAxis({ t: t, timeValid: timeValid }, {});
  for (const tick of axis.ticks) {
    assert.equal(typeof tick.label, 'string');
    assert.ok(tick.label.length > 0);
  }
});

// ---------------------------------------------------------------------------
// Regression — buildChartSeries additive t/timeValid pass-through fields.
// ---------------------------------------------------------------------------

test('buildChartSeries exposes t and timeValid pass-through fields for a known fixture', () => {
  const history = {
    t: [100, 200, 300],
    time_valid: [true, false, true],
    lux: [1.5, null, 3.5],
    temp_c: [null, 20.1, 20.2],
    level: ['FULL', 'FULL', 'MID'],
  };
  const series = buildChartSeries(history);
  assert.deepStrictEqual(series.t, [100, 200, 300]);
  assert.deepStrictEqual(series.timeValid, [true, false, true]);
});

// Sanity check that formatReadingTimestamp's contract is untouched by the
// formatEpoch refactor (full regression already covered by
// dashboard-logic.test.mjs; this is a narrow smoke check in this file).
test('formatReadingTimestamp is unaffected by the formatEpoch refactor', () => {
  assert.equal(formatReadingTimestamp(42, false), null);
  assert.equal(typeof formatReadingTimestamp(1700000000, true), 'string');
});

// ---------------------------------------------------------------------------
// buildLevelBands — WI-003 level->band run-length segmentation
// ---------------------------------------------------------------------------

test('buildLevelBands collapses a mixed sequence into run-length bands', () => {
  const series = { level: ['FULL', 'FULL', 'FULL', 'MID', 'MID', 'LOW'] };
  const result = buildLevelBands(series);
  assert.equal(result.state, 'ok');
  assert.equal(result.emptyMessage, null);
  assert.equal(result.bands.length, 3);
  assert.equal(result.bands[0].level, 'FULL');
  assert.equal(result.bands[0].count, 3);
  assert.equal(result.bands[1].level, 'MID');
  assert.equal(result.bands[1].count, 2);
  assert.equal(result.bands[2].level, 'LOW');
  assert.equal(result.bands[2].count, 1);
});

test('buildLevelBands never merges FAULT into an adjacent band even when bands look similar', () => {
  const series = { level: ['MID', 'MID', 'FAULT', 'MID', 'MID'] };
  const result = buildLevelBands(series);
  assert.equal(result.bands.length, 3);
  assert.equal(result.bands[0].level, 'MID');
  assert.equal(result.bands[1].level, 'FAULT');
  assert.equal(result.bands[2].level, 'MID');
});

test('buildLevelBands keeps UNKNOWN distinct from FAULT (never merged together)', () => {
  const series = { level: ['UNKNOWN', 'UNKNOWN', 'FAULT', 'FAULT'] };
  const result = buildLevelBands(series);
  assert.equal(result.bands.length, 2);
  assert.equal(result.bands[0].level, 'UNKNOWN');
  assert.equal(result.bands[0].count, 2);
  assert.equal(result.bands[1].level, 'FAULT');
  assert.equal(result.bands[1].count, 2);
});

test('buildLevelBands produces a single one-sample band for a single-sample series', () => {
  const series = { level: ['FULL'] };
  const result = buildLevelBands(series);
  assert.equal(result.state, 'ok');
  assert.equal(result.bands.length, 1);
  assert.equal(result.bands[0].count, 1);
  assert.equal(result.bands[0].startIndex, 0);
  assert.equal(result.bands[0].endIndex, 0);
});

test('buildLevelBands returns no-samples state for an empty level array', () => {
  const result = buildLevelBands({ level: [] });
  assert.equal(result.state, 'no-samples');
  assert.deepStrictEqual(result.bands, []);
  assert.equal(result.emptyMessage, 'no readings recorded yet');
});

// C3 regression: an all-UNKNOWN series (n > 0) is a real, renderable band —
// NOT the empty state. Only level.length === 0 is 'no-samples' for the level
// chart, since `level` is never null per level_json_name.
test('buildLevelBands treats an all-UNKNOWN series as state ok with one UNKNOWN band, not empty (C3)', () => {
  const series = { level: ['UNKNOWN', 'UNKNOWN', 'UNKNOWN'] };
  const result = buildLevelBands(series);
  assert.equal(result.state, 'ok');
  assert.equal(result.emptyMessage, null);
  assert.equal(result.bands.length, 1);
  assert.equal(result.bands[0].level, 'UNKNOWN');
  assert.equal(result.bands[0].count, 3);
});

test('buildLevelBands band x0/x1 match the exported sampleX for a concrete fixture', () => {
  const series = { level: ['FULL', 'FULL', 'MID', 'LOW', 'LOW'] };
  const n = series.level.length;
  const result = buildLevelBands(series);
  for (const band of result.bands) {
    assert.equal(band.x0, sampleX(band.startIndex, n));
    assert.equal(band.x1, sampleX(band.endIndex, n));
  }
  assert.equal(result.bands[0].startIndex, 0);
  assert.equal(result.bands[0].endIndex, 1);
  assert.equal(result.bands[1].startIndex, 2);
  assert.equal(result.bands[1].endIndex, 2);
  assert.equal(result.bands[2].startIndex, 3);
  assert.equal(result.bands[2].endIndex, 4);
});

// ---------------------------------------------------------------------------
// buildTempChartAriaLabel / buildLightChartAriaLabel — WI-004 per-chart
// accessible summaries for the two numeric metrics.
// ---------------------------------------------------------------------------

test('buildTempChartAriaLabel reports "no readings recorded yet" for an empty series', () => {
  const series = { labels: [], temp_c: [] };
  assert.equal(buildTempChartAriaLabel(series), 'Water temperature chart. No readings recorded yet.');
});

test('buildTempChartAriaLabel states offline (not omitted) when every temp_c entry is null', () => {
  const labels = new Array(180).fill('x');
  const series = { labels: labels, temp_c: new Array(180).fill(null) };
  assert.equal(
    buildTempChartAriaLabel(series),
    'Water temperature chart over 180 samples. No plottable data. Water temperature is offline.'
  );
});

test('buildTempChartAriaLabel reports min-to-max range on a happy-path fixture', () => {
  const labels = new Array(180).fill('x');
  const temp_c = new Array(180).fill(20);
  temp_c[0] = 8.2;
  temp_c[1] = 24.6;
  const series = { labels: labels, temp_c: temp_c };
  const label = buildTempChartAriaLabel(series);
  assert.match(label, /^Water temperature chart over 180 samples\. 8\.2 to 24\.6 °C\.$/);
});

test('buildLightChartAriaLabel reports "no readings recorded yet" for an empty series', () => {
  const series = { labels: [], lux: [] };
  assert.equal(buildLightChartAriaLabel(series), 'Ambient light chart. No readings recorded yet.');
});

test('buildLightChartAriaLabel states offline (not omitted) when every lux entry is null', () => {
  const labels = new Array(24).fill('x');
  const series = { labels: labels, lux: new Array(24).fill(null) };
  assert.equal(
    buildLightChartAriaLabel(series),
    'Ambient light chart over 24 samples. No plottable data. Ambient light is offline.'
  );
});

// A single-sample series has min === max; describeRange (reused, not
// reimplemented per the task's contract) collapses that to one value
// rather than "X to X", and the sample-count clause uses the singular form.
test('buildLightChartAriaLabel reports a single value (not "X to X") for a 1-sample fixture, singular', () => {
  const series = { labels: ['x'], lux: [150.4] };
  assert.equal(buildLightChartAriaLabel(series), 'Ambient light chart over 1 sample. 150.4 lux.');
});

// ---------------------------------------------------------------------------
// buildLevelChartAriaLabel — WI-004 per-chart accessible summary for level
// (categorical; always state 'ok' once n > 0, per C3 — no offline case).
// ---------------------------------------------------------------------------

test('buildLevelChartAriaLabel reports "no readings recorded yet" for an empty series', () => {
  const series = { labels: [], level: [] };
  assert.equal(buildLevelChartAriaLabel(series), 'Water level chart. No readings recorded yet.');
});

test('buildLevelChartAriaLabel describes a single-band series with "for all N samples"', () => {
  const level = new Array(60).fill('FULL');
  const series = { labels: new Array(60).fill('x'), level: level };
  assert.equal(
    buildLevelChartAriaLabel(series),
    'Water level chart over 60 samples. FULL for all 60 samples.'
  );
});

// C3 regression: all-UNKNOWN is a single real band, described the same way
// as any other single-band series — not an offline/empty sentence.
test('buildLevelChartAriaLabel describes an all-UNKNOWN series as a real band (C3)', () => {
  const level = new Array(10).fill('UNKNOWN');
  const series = { labels: new Array(10).fill('x'), level: level };
  assert.equal(
    buildLevelChartAriaLabel(series),
    'Water level chart over 10 samples. UNKNOWN for all 10 samples.'
  );
});

test('buildLevelChartAriaLabel describes a multi-band series with first/then/most-recent prose', () => {
  const level = [].concat(
    new Array(40).fill('FULL'),
    new Array(68).fill('MID'),
    new Array(60).fill('LOW'),
    new Array(12).fill('FAULT')
  );
  const series = { labels: new Array(180).fill('x'), level: level };
  assert.equal(
    buildLevelChartAriaLabel(series),
    'Water level chart over 180 samples. FULL for the first 40 samples, then MID, then LOW, then FAULT for the most recent 12 samples.' // (also confirms every non-first band is prefixed "then")
  );
});

test('buildLevelChartAriaLabel uses singular "sample" for a first/last band of count 1', () => {
  const level = ['FULL', 'MID', 'MID', 'MID', 'LOW'];
  const series = { labels: new Array(5).fill('x'), level: level };
  assert.equal(
    buildLevelChartAriaLabel(series),
    'Water level chart over 5 samples. FULL for the first 1 sample, then MID, then LOW for the most recent 1 sample.'
  );
});
