// dashboard-logic.test.mjs — host tests for the pure browser-side logic
// module (src/web/dashboard-logic.js). Run with Node's built-in test runner:
//   node --test test/web/*.test.mjs
//
// This is the browser-side instance of the project's Pure-Logic /
// Device-Only Split (systemPatterns.md): dashboard-logic.js has zero DOM
// dependency, so it is host-testable the same way lib/reading_json and
// lib/reading_store_core are testable under [env:native]. app.js (the DOM
// half), index.html, style.css, and http_api.c's new routes remain
// bench-verify-only per the Phase 6 Test Strategy exception — only this
// file needed host tests.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import DashboardLogic from '../../src/web/dashboard-logic.js';

const {
  formatReadingTimestamp,
  deriveMetricBadge,
  deriveLevelBadge,
  isPreFirstSample,
  buildChartSeries,
  hasPlottableData,
  buildChartAriaLabel,
} = DashboardLogic;

// ---------------------------------------------------------------------------
// formatReadingTimestamp — must clearly distinguish time_valid:false from a
// real timestamp; never silently render a near-1970 date as if it were real.
// ---------------------------------------------------------------------------

test('formatReadingTimestamp returns null when time_valid is false', () => {
  // A near-1970 epoch value is exactly what an un-synced RTC reports —
  // this must never be rendered as if it were a real time.
  assert.equal(formatReadingTimestamp(42, false), null);
});

test('formatReadingTimestamp returns null for a near-1970 epoch even if time_valid were mistakenly true-ish', () => {
  // time_valid is the authoritative signal; when false, format must not
  // fall back to guessing from the magnitude of t.
  assert.equal(formatReadingTimestamp(0, false), null);
});

test('formatReadingTimestamp returns a formatted string for a valid, real timestamp', () => {
  // 1700000000 = 2023-11-14T22:13:20Z — a plausible real epoch.
  const result = formatReadingTimestamp(1700000000, true);
  assert.equal(typeof result, 'string');
  assert.notEqual(result, null);
  assert.notEqual(result.length, 0);
});

// ---------------------------------------------------------------------------
// deriveMetricBadge — invalid -> "offline", valid -> "live" (distinct css
// classes so state is conveyed by text, not color alone).
// ---------------------------------------------------------------------------

test('deriveMetricBadge marks an invalid reading as offline', () => {
  const badge = deriveMetricBadge(false);
  assert.equal(badge.text.toLowerCase(), 'offline');
  assert.ok(badge.cssClass.length > 0);
});

test('deriveMetricBadge marks a valid reading as live', () => {
  const badge = deriveMetricBadge(true);
  assert.equal(badge.text.toLowerCase(), 'live');
});

test('deriveMetricBadge uses different css classes for live vs offline', () => {
  const live = deriveMetricBadge(true);
  const offline = deriveMetricBadge(false);
  assert.notEqual(live.cssClass, offline.cssClass);
});

// ---------------------------------------------------------------------------
// deriveLevelBadge — FAULT must be surfaced as FAULT (never resolved to a
// neighboring band); UNKNOWN gets its own text too.
// ---------------------------------------------------------------------------

test('deriveLevelBadge surfaces FAULT distinctly from FULL/MID/LOW', () => {
  const fault = deriveLevelBadge('FAULT');
  const full = deriveLevelBadge('FULL');
  const mid = deriveLevelBadge('MID');
  const low = deriveLevelBadge('LOW');
  assert.equal(fault.text, 'FAULT');
  assert.notEqual(fault.cssClass, full.cssClass);
  assert.notEqual(fault.cssClass, mid.cssClass);
  assert.notEqual(fault.cssClass, low.cssClass);
});

test('deriveLevelBadge passes through each known band as its own text', () => {
  assert.equal(deriveLevelBadge('FULL').text, 'FULL');
  assert.equal(deriveLevelBadge('MID').text, 'MID');
  assert.equal(deriveLevelBadge('LOW').text, 'LOW');
});

test('deriveLevelBadge handles UNKNOWN as its own distinct badge', () => {
  const unknown = deriveLevelBadge('UNKNOWN');
  const fault = deriveLevelBadge('FAULT');
  assert.equal(unknown.text, 'UNKNOWN');
  assert.notEqual(unknown.cssClass, fault.cssClass);
});

// ---------------------------------------------------------------------------
// isPreFirstSample — AC-ASYNC-1: level UNKNOWN + nothing valid means no
// sample has completed yet; must be detected so the UI shows "waiting for
// first reading" instead of a 0/blank/dash.
// ---------------------------------------------------------------------------

test('isPreFirstSample is true when level is UNKNOWN and nothing is valid', () => {
  const payload = {
    t: 0,
    time_valid: false,
    lux: null,
    temp_c: null,
    level: 'UNKNOWN',
    valid: { light: false, temp: false, level: false },
  };
  assert.equal(isPreFirstSample(payload), true);
});

test('isPreFirstSample is false once a real sample has completed (level no longer UNKNOWN)', () => {
  const payload = {
    t: 1700000000,
    time_valid: true,
    lux: 123.45,
    temp_c: 21.5,
    level: 'FULL',
    valid: { light: true, temp: true, level: true },
  };
  assert.equal(isPreFirstSample(payload), false);
});

test('isPreFirstSample is false when level is FAULT (a real, completed sample state)', () => {
  const payload = {
    t: 1700000000,
    time_valid: true,
    lux: null,
    temp_c: 21.5,
    level: 'FAULT',
    valid: { light: false, temp: true, level: false },
  };
  assert.equal(isPreFirstSample(payload), false);
});

// ---------------------------------------------------------------------------
// buildChartSeries — must preserve null gaps rather than coercing to 0, and
// keep all series index-aligned with the source history payload.
// ---------------------------------------------------------------------------

test('buildChartSeries preserves null entries as gaps instead of coercing to 0', () => {
  const history = {
    t: [100, 200, 300],
    time_valid: [true, true, true],
    lux: [1.5, null, 3.5],
    temp_c: [null, 20.1, 20.2],
    level: ['FULL', 'FULL', 'MID'],
  };
  const series = buildChartSeries(history);
  assert.equal(series.lux[1], null);
  assert.equal(series.temp_c[0], null);
  assert.notEqual(series.lux[1], 0);
  assert.notEqual(series.temp_c[0], 0);
});

test('buildChartSeries produces index-aligned series of the same length as the input', () => {
  const history = {
    t: [100, 200, 300],
    time_valid: [true, true, true],
    lux: [1.5, 2.5, 3.5],
    temp_c: [20.0, 20.1, 20.2],
    level: ['FULL', 'FULL', 'MID'],
  };
  const series = buildChartSeries(history);
  assert.equal(series.labels.length, 3);
  assert.equal(series.lux.length, 3);
  assert.equal(series.temp_c.length, 3);
  assert.equal(series.level.length, 3);
  assert.deepEqual(series.level, ['FULL', 'FULL', 'MID']);
});

test('buildChartSeries handles an empty history payload without throwing', () => {
  const history = { t: [], time_valid: [], lux: [], temp_c: [], level: [] };
  const series = buildChartSeries(history);
  assert.equal(series.labels.length, 0);
  assert.equal(series.lux.length, 0);
});

// ---------------------------------------------------------------------------
// hasPlottableData — distinguishes "chart is empty because there is genuinely
// nothing numeric to draw" from "chart is empty because it broke". Added after
// bench bring-up (2026-08-20): with both sensors unplugged every lux/temp_c
// value is null, and the canvas rendered as a blank box indistinguishable from
// a failed render. The renderer needs an explicit predicate to branch on.
// ---------------------------------------------------------------------------

test('hasPlottableData is false when every numeric value is null', () => {
  // Exactly the observed bench state: both sensors offline, level still
  // reporting. level is categorical and is not plotted, so it must not
  // count as plottable data.
  const series = {
    labels: ['a', 'b', 'c'],
    lux: [null, null, null],
    temp_c: [null, null, null],
    level: ['FULL', 'FULL', 'FULL'],
  };
  assert.equal(hasPlottableData(series), false);
});

test('hasPlottableData is false for an empty series', () => {
  assert.equal(hasPlottableData({ labels: [], lux: [], temp_c: [], level: [] }), false);
});

test('hasPlottableData is true when a single finite value exists in either series', () => {
  const luxOnly = { labels: ['a', 'b'], lux: [null, 12.5], temp_c: [null, null], level: [] };
  const tempOnly = { labels: ['a', 'b'], lux: [null, null], temp_c: [19.25, null], level: [] };
  assert.equal(hasPlottableData(luxOnly), true);
  assert.equal(hasPlottableData(tempOnly), true);
});

test('hasPlottableData ignores non-finite numbers that are not null', () => {
  // NaN/Infinity must not be treated as plottable — plotSeries filters on
  // isFinite, so the predicate has to agree with it or the two disagree
  // about whether to draw the empty state.
  const series = {
    labels: ['a', 'b'],
    lux: [NaN, Infinity],
    temp_c: [-Infinity, NaN],
    level: [],
  };
  assert.equal(hasPlottableData(series), false);
});

test('hasPlottableData tolerates a malformed series without throwing', () => {
  assert.equal(hasPlottableData(null), false);
  assert.equal(hasPlottableData({}), false);
});

// ---------------------------------------------------------------------------
// buildChartAriaLabel — the <canvas> carried no role, no aria-label and no
// fallback content, so it did not appear in the accessibility tree at all
// (verified in Chrome at the bench): a screen-reader user got the three tiles
// and no indication the 24-hour history existed. This builds the text
// equivalent app.js attaches to the canvas.
// ---------------------------------------------------------------------------

test('buildChartAriaLabel describes the empty case without implying a broken chart', () => {
  const series = {
    labels: ['a', 'b'],
    lux: [null, null],
    temp_c: [null, null],
    level: ['FULL', 'FULL'],
  };
  const label = buildChartAriaLabel(series);
  assert.match(label, /no .*data/i);
  // Must not claim a reading range it does not have.
  assert.doesNotMatch(label, /\d+(\.\d+)?\s*(°C|lux)/);
});

test('buildChartAriaLabel reports no readings at all for an empty series', () => {
  const label = buildChartAriaLabel({ labels: [], lux: [], temp_c: [], level: [] });
  assert.match(label, /no readings/i);
});

test('buildChartAriaLabel states the range and sample count for each plotted series', () => {
  const series = {
    labels: ['a', 'b', 'c'],
    lux: [100, 250, 400],
    temp_c: [18.5, 19, 20.25],
    level: ['FULL', 'FULL', 'MID'],
  };
  const label = buildChartAriaLabel(series);
  assert.match(label, /18\.5/);
  assert.match(label, /20\.25|20\.3/);
  assert.match(label, /100/);
  assert.match(label, /400/);
  assert.match(label, /3 samples/);
});

test('buildChartAriaLabel omits a series that has no finite values', () => {
  // Temperature offline, light working: the label must describe light and
  // say temperature is offline, not invent a temperature range.
  const series = {
    labels: ['a', 'b'],
    lux: [100, 200],
    temp_c: [null, null],
    level: ['FULL', 'FULL'],
  };
  const label = buildChartAriaLabel(series);
  assert.match(label, /100/);
  assert.match(label, /temperature/i);
  assert.match(label, /offline/i);
  assert.doesNotMatch(label, /temperature[^.]*\d+(\.\d+)?\s*°C/i);
});

test('buildChartAriaLabel tolerates a malformed series without throwing', () => {
  assert.equal(typeof buildChartAriaLabel(null), 'string');
  assert.equal(typeof buildChartAriaLabel({}), 'string');
});
