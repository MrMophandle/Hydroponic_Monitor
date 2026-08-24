/**
 * dashboard-logic — pure, DOM-free logic for the embedded web dashboard.
 *
 * Browser-side instance of the project's Pure-Logic / Device-Only Split
 * (systemPatterns.md): this module touches no `document`, `window.fetch`,
 * timer, or any other browser-only API, so it is host-testable with Node's
 * built-in test runner (see test/web/dashboard-logic.test.mjs) exactly the
 * way lib/reading_json and lib/reading_store_core are host-testable under
 * [env:native]. app.js is the thin, DOM-only device-only half: it owns
 * fetch(), setInterval(), and all element updates, and delegates every
 * interpretation decision (badge text/class, timestamp formatting, gap
 * handling, pre-first-sample detection) to the functions here.
 *
 * UMD-style export: a plain global (`DashboardLogic`) in the browser via
 * <script>, and a CommonJS `module.exports` object under Node so the test
 * file can `import DashboardLogic from '../../src/web/dashboard-logic.js'`
 * with zero configuration and zero dependencies.
 */
(function (root) {
  'use strict';

  // Presentation constants — a narrow, pre-authorized deviation from
  // "Configuration Is Not Hard-Coded" (systemPatterns.md), same precedent as
  // POLL_INTERVAL_MS in app.js. Density hints for the numeric Y axis and the
  // (fewer, wider-label) time X axis, plus a float-snapping guard for tick
  // label decimal counts.
  var Y_AXIS_TARGET_TICKS = 5;
  var X_AXIS_TARGET_TICKS = 4;
  var MAX_LABEL_DECIMALS = 6;

  /**
   * Shared honesty rule behind formatReadingTimestamp/formatAxisTimestamp:
   * never derive a label from a timestamp the server has not vouched for.
   * This board has no battery-backed RTC (systemPatterns.md "Known open
   * items"), so before SNTP sync `epoch_sec` reads near-1970 — rendering
   * that as if it were a real date would be worse than showing nothing.
   * `timeOnly` selects locale time-of-day (axis ticks) vs. full
   * locale date+time (reading timestamps / captions).
   */
  function formatEpoch(epochSec, timeValid, timeOnly) {
    if (!timeValid) {
      return null;
    }
    var date = new Date(epochSec * 1000);
    return timeOnly ? date.toLocaleTimeString() : date.toLocaleString();
  }

  /**
   * Formats an epoch-seconds timestamp for display, but ONLY when the
   * server has told us the clock is trustworthy. Returns `null` when
   * `timeValid` is false so the caller can render a "clock not synced"
   * placeholder instead of a misleading date.
   */
  function formatReadingTimestamp(epochSec, timeValid) {
    return formatEpoch(epochSec, timeValid, false);
  }

  /**
   * Time-only variant of formatReadingTimestamp, for compact axis tick
   * labels (e.g. chart X axis) — same honesty rule, shorter format.
   */
  function formatAxisTimestamp(epochSec, timeValid) {
    return formatEpoch(epochSec, timeValid, true);
  }

  /**
   * Derives the live/offline badge for a single metric (light or
   * temperature) from its per-sample `valid` bit, as delivered by the
   * server. AC-ERROR-1: the server already tracks the >=5-consecutive-
   * failure threshold before flipping a reading invalid — the client's job
   * is only to reflect the bit it was given, not to re-derive the
   * threshold itself.
   */
  function deriveMetricBadge(isValid) {
    return isValid
      ? { text: 'live', cssClass: 'badge-live' }
      : { text: 'offline', cssClass: 'badge-offline' };
  }

  /**
   * Derives the water-level badge. Every known band gets its own text and
   * its own css class so state is conveyed by words, not color alone
   * (Accessibility NFR), and so FAULT can never be visually confused with
   * a neighboring band (AC-ERROR-2) nor with UNKNOWN (the pre-first-sample
   * lifecycle state, not a switch reading).
   */
  function deriveLevelBadge(levelStr) {
    var known = {
      FULL: 'badge-level-full',
      MID: 'badge-level-mid',
      LOW: 'badge-level-low',
      FAULT: 'badge-level-fault',
      UNKNOWN: 'badge-level-unknown',
    };
    var cssClass = known[levelStr] || 'badge-level-unknown';
    return { text: levelStr, cssClass: cssClass };
  }

  /**
   * AC-ASYNC-1: true only before the very first sample has completed —
   * `level === "UNKNOWN"` is the lifecycle signal (not a switch reading),
   * confirmed by every per-sample `valid` bit also being false. Once a
   * real sample lands (even an all-invalid/FAULT one), level moves off
   * UNKNOWN and this returns false — the UI has real state to show,
   * however degraded.
   */
  function isPreFirstSample(nowPayload) {
    if (!nowPayload || nowPayload.level !== 'UNKNOWN') {
      return false;
    }
    var valid = nowPayload.valid || {};
    return !valid.light && !valid.temp && !valid.level;
  }

  /**
   * Transforms a raw /api/history payload into index-aligned chart series.
   * `null` entries in `lux`/`temp_c` pass through UNCHANGED (never coerced
   * to 0) so the caller's chart renderer draws a gap instead of a false
   * plunge to zero.
   */
  function buildChartSeries(historyPayload) {
    var t = (historyPayload && historyPayload.t) || [];
    var timeValid = (historyPayload && historyPayload.time_valid) || [];
    var lux = (historyPayload && historyPayload.lux) || [];
    var tempC = (historyPayload && historyPayload.temp_c) || [];
    var level = (historyPayload && historyPayload.level) || [];

    var labels = t.map(function (epochSec, i) {
      return formatReadingTimestamp(epochSec, timeValid[i]);
    });

    return {
      labels: labels,
      t: t.slice(),
      timeValid: timeValid.slice(),
      lux: lux.slice(),
      temp_c: tempC.slice(),
      level: level.slice(),
    };
  }

  /**
   * Finite-value range for one numeric series, or null when the series has
   * nothing plottable. `isFinite` is the same predicate app.js's plotSeries
   * uses — the two MUST agree, or the renderer and the empty-state check
   * disagree about whether there is anything to draw.
   */
  function finiteRange(values) {
    if (!values || !values.length) {
      return null;
    }
    var min = null;
    var max = null;
    var count = 0;
    for (var i = 0; i < values.length; i++) {
      var v = values[i];
      if (typeof v !== 'number' || !isFinite(v)) {
        continue;
      }
      count++;
      if (min === null || v < min) {
        min = v;
      }
      if (max === null || v > max) {
        max = v;
      }
    }
    return count === 0 ? null : { min: min, max: max, count: count };
  }

  /**
   * True when the chart has at least one finite point to draw.
   *
   * Added after bench bring-up (2026-08-20). With both sensors unplugged
   * every lux/temp_c entry is null, so the canvas rendered as a blank
   * 900x320 box — visually indistinguishable from a chart that failed to
   * render. The renderer needs an explicit signal to branch on so it can
   * say "no data yet" instead of showing nothing at all.
   *
   * `level` is deliberately NOT considered: it is categorical and is not
   * plotted, so a working level switch must not suppress the empty state
   * for the two numeric series.
   *
   * Superseded, per-metric, by `buildMetricAxis(...).state` (Phase 1 of
   * per-metric-dashboard-charts-with-labeled-axes) — kept as-is, unchanged
   * behavior, for callers still checking combined plottability.
   */
  function hasPlottableData(series) {
    if (!series) {
      return false;
    }
    return finiteRange(series.lux) !== null || finiteRange(series.temp_c) !== null;
  }

  /**
   * The ONE shared index -> X-fraction mapping used by both a metric axis's
   * plotted segments and a time axis's ticks. A single sample maps to X=0
   * (the `Math.max(sampleCount - 1, 1)` guard avoids a divide-by-zero).
   */
  function sampleX(index, sampleCount) {
    return index / Math.max(sampleCount - 1, 1);
  }

  /**
   * "Nice" tick step for a numeric axis: rounds span/target up to the
   * nearest 1/2/5/10 x a power of ten, so tick values land on human-legible
   * numbers (0.5, 1, 2, 5, 10, 20, 50, ...) instead of arbitrary fractions.
   */
  function niceStep(span, target) {
    var raw = span / target;
    var mag = Math.pow(10, Math.floor(Math.log10(raw)));
    var norm = raw / mag;
    var mult = norm <= 1 ? 1 : norm <= 2 ? 2 : norm <= 5 ? 5 : 10;
    return mag * mult;
  }

  function clamp(v, lo, hi) {
    return Math.max(lo, Math.min(hi, v));
  }

  /**
   * Plain fixed-decimal tick label — no thousands separator, no `k` suffix,
   * no `toLocaleString`. Normalizes -0 to +0 first so `toFixed` never prints
   * a confusing '-0'/'-0.0' for a domain straddling zero.
   */
  function formatTickLabel(value, decimals) {
    if (value === 0) {
      value = 0;
    }
    return value.toFixed(decimals);
  }

  /**
   * Builds the numeric axis (domain, "nice" ticks, tick labels/positions,
   * and plottable segments) for ONE metric's series in isolation. The
   * single-array signature is deliberate: a caller passes exactly one
   * metric's values (e.g. series.temp_c), so a stray value in a sibling
   * series (e.g. series.lux) can never leak into this axis's domain — the
   * per-chart split this task exists to deliver.
   *
   * Three states, discriminated ONLY by `state` (no redundant boolean
   * predicate):
   *   - 'no-samples': values is empty.
   *   - 'no-finite-values': values has entries, but none are finite numbers
   *     (matches the same isFinite-style gate as the private finiteRange
   *     helper above, so the renderer and this axis never disagree about
   *     what counts as plottable).
   *   - 'ok': at least one finite value; domain/ticks/segments are built.
   */
  function buildMetricAxis(values, options) {
    var opts = options || {};
    var unit = opts.unit;
    var axisTitle = opts.title ? opts.title + ' (' + unit + ')' : unit;

    if (!values || values.length === 0) {
      return {
        state: 'no-samples',
        unit: unit,
        axisTitle: axisTitle,
        emptyMessage: 'no readings recorded yet',
        dataRange: null,
        domain: null,
        step: null,
        decimals: null,
        ticks: [],
        tickLabels: [],
        tickPositions: [],
        segments: [],
      };
    }

    var range = finiteRange(values);
    if (range === null) {
      return {
        state: 'no-finite-values',
        unit: unit,
        axisTitle: axisTitle,
        emptyMessage: 'no data to plot — sensor offline',
        dataRange: null,
        domain: null,
        step: null,
        decimals: null,
        ticks: [],
        tickLabels: [],
        tickPositions: [],
        segments: [],
      };
    }

    var dataRange = { min: range.min, max: range.max, count: range.count };

    var lo = dataRange.min;
    var hi = dataRange.max;
    if (lo === hi) {
      var pad = Math.abs(lo) * 0.05 || 1;
      lo -= pad;
      hi += pad;
    }

    var targetTickCount = opts.targetTickCount || Y_AXIS_TARGET_TICKS;
    var step = niceStep(hi - lo, targetTickCount);
    var decimals = clamp(-Math.floor(Math.log10(step)), 0, MAX_LABEL_DECIMALS);
    var scale = Math.pow(10, decimals);

    var i0 = Math.floor(lo / step);
    var i1 = Math.ceil(hi / step);
    var ticks = [];
    for (var k = 0; k <= i1 - i0; k++) {
      ticks[k] = Math.round((i0 + k) * step * scale) / scale;
    }

    var domain = { min: ticks[0], max: ticks[ticks.length - 1] };
    var span = domain.max - domain.min;

    var tickLabels = [];
    var tickPositions = [];
    for (var t = 0; t < ticks.length; t++) {
      tickLabels[t] = formatTickLabel(ticks[t], decimals);
      tickPositions[t] = (ticks[t] - domain.min) / span;
    }

    var segments = [];
    var currentSegment = null;
    for (var i = 0; i < values.length; i++) {
      var v = values[i];
      if (typeof v === 'number' && isFinite(v)) {
        if (!currentSegment) {
          currentSegment = [];
          segments.push(currentSegment);
        }
        currentSegment.push({
          index: i,
          value: v,
          x: sampleX(i, values.length),
          y: (v - domain.min) / span,
        });
      } else {
        currentSegment = null;
      }
    }

    return {
      state: 'ok',
      unit: unit,
      axisTitle: axisTitle,
      emptyMessage: null,
      dataRange: dataRange,
      domain: domain,
      step: step,
      decimals: decimals,
      ticks: ticks,
      tickLabels: tickLabels,
      tickPositions: tickPositions,
      segments: segments,
    };
  }

  /**
   * Builds the time (X) axis ticks + caption for a chart's shared time
   * base, honoring the same "never render an unsynced timestamp" rule as
   * formatReadingTimestamp/formatAxisTimestamp: a tick can only ever be
   * derived from an index where `series.timeValid[i]` is true.
   *
   * PRECONDITION: Assumes input samples (series.t) are evenly spaced in
   * time. This spacing is enforced by `src/sampler.c` via `xTaskDelayUntil()`
   * on the device; without it, the X-axis scale would misrepresent time gaps.
   */
  function buildTimeAxis(series, options) {
    var opts = options || {};
    var t = (series && series.t) || [];
    var timeValid = (series && series.timeValid) || [];
    var n = t.length;

    if (n === 0) {
      return {
        state: 'no-samples',
        sampleCount: 0,
        validCount: 0,
        ticks: [],
        caption: 'no readings recorded yet',
      };
    }

    var validIndices = [];
    for (var i = 0; i < n; i++) {
      if (timeValid[i] === true && typeof t[i] === 'number' && isFinite(t[i])) {
        validIndices.push(i);
      }
    }

    if (validIndices.length === 0) {
      return {
        state: 'clock-not-synced',
        sampleCount: n,
        validCount: 0,
        ticks: [],
        caption: 'clock not synced',
      };
    }

    var target = (opts && opts.targetTickCount) || X_AXIS_TARGET_TICKS;
    var chosen;
    if (validIndices.length <= target) {
      chosen = validIndices;
    } else {
      chosen = [];
      for (var j = 0; j < target; j++) {
        chosen[j] = validIndices[Math.round((j * (validIndices.length - 1)) / (target - 1))];
      }
    }

    var ticks = [];
    for (var c = 0; c < chosen.length; c++) {
      var idx = chosen[c];
      ticks.push({
        index: idx,
        epochSec: t[idx],
        x: sampleX(idx, n),
        label: formatAxisTimestamp(t[idx], true),
      });
    }

    var firstValid = validIndices[0];
    var lastValid = validIndices[validIndices.length - 1];
    var caption =
      formatReadingTimestamp(t[firstValid], true) + ' – ' + formatReadingTimestamp(t[lastValid], true);
    if (validIndices.length < n) {
      caption += ' (earlier samples: clock not synced)';
    }

    return {
      state: 'ok',
      sampleCount: n,
      validCount: validIndices.length,
      ticks: ticks,
      caption: caption,
    };
  }

  function formatNumber(v) {
    return String(Math.round(v * 100) / 100);
  }

  function describeRange(range, unit) {
    if (range.min === range.max) {
      return formatNumber(range.min) + ' ' + unit;
    }
    return formatNumber(range.min) + ' to ' + formatNumber(range.max) + ' ' + unit;
  }

  /**
   * Text equivalent of the history chart, for the canvas's aria-label.
   *
   * The <canvas> shipped with no role, no aria-label and no fallback
   * content, so it did not appear in the accessibility tree at all
   * (verified in Chrome at the bench, 2026-08-20): a screen-reader user got
   * the three metric tiles and no indication that 24 hours of history
   * existed. A canvas cannot expose its pixels to assistive tech, so the
   * only honest fix is an explicit summary.
   *
   * Never invents a range it does not have — an offline series is reported
   * as offline rather than omitted silently, so the reader can tell the
   * difference between "no light data" and "light data I forgot to mention".
   */
  function buildChartAriaLabel(series) {
    var labels = (series && series.labels) || [];
    var n = labels.length;
    if (n === 0) {
      return '24-hour history chart. No readings recorded yet.';
    }

    var sampleCount = n + (n === 1 ? ' sample' : ' samples');
    var tempRange = finiteRange(series.temp_c);
    var luxRange = finiteRange(series.lux);

    if (tempRange === null && luxRange === null) {
      return (
        '24-hour history chart over ' +
        sampleCount +
        '. No plottable data. Water temperature and ambient light are both offline.'
      );
    }

    var parts = ['24-hour history chart over ' + sampleCount + '.'];
    parts.push(
      tempRange !== null
        ? 'Water temperature ' + describeRange(tempRange, '°C') + '.'
        : 'Water temperature offline.'
    );
    parts.push(
      luxRange !== null
        ? 'Ambient light ' + describeRange(luxRange, 'lux') + '.'
        : 'Ambient light offline.'
    );
    return parts.join(' ');
  }

  var DashboardLogic = {
    formatReadingTimestamp: formatReadingTimestamp,
    formatAxisTimestamp: formatAxisTimestamp,
    deriveMetricBadge: deriveMetricBadge,
    deriveLevelBadge: deriveLevelBadge,
    isPreFirstSample: isPreFirstSample,
    buildChartSeries: buildChartSeries,
    hasPlottableData: hasPlottableData,
    buildChartAriaLabel: buildChartAriaLabel,
    buildMetricAxis: buildMetricAxis,
    buildTimeAxis: buildTimeAxis,
    sampleX: sampleX,
  };

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = DashboardLogic;
  } else {
    root.DashboardLogic = DashboardLogic;
  }
})(typeof window !== 'undefined' ? window : globalThis);
