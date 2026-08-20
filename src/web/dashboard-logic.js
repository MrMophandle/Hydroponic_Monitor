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

  /**
   * Formats an epoch-seconds timestamp for display, but ONLY when the
   * server has told us the clock is trustworthy. This board has no
   * battery-backed RTC (systemPatterns.md "Known open items"), so before
   * SNTP sync `epoch_sec` reads near-1970 — rendering that as if it were a
   * real date would be worse than showing nothing. Returns `null` when
   * `timeValid` is false so the caller can render a "clock not synced"
   * placeholder instead of a misleading date.
   */
  function formatReadingTimestamp(epochSec, timeValid) {
    if (!timeValid) {
      return null;
    }
    var date = new Date(epochSec * 1000);
    return date.toLocaleString();
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
   */
  function hasPlottableData(series) {
    if (!series) {
      return false;
    }
    return finiteRange(series.lux) !== null || finiteRange(series.temp_c) !== null;
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
    deriveMetricBadge: deriveMetricBadge,
    deriveLevelBadge: deriveLevelBadge,
    isPreFirstSample: isPreFirstSample,
    buildChartSeries: buildChartSeries,
    hasPlottableData: hasPlottableData,
    buildChartAriaLabel: buildChartAriaLabel,
  };

  if (typeof module !== 'undefined' && module.exports) {
    module.exports = DashboardLogic;
  } else {
    root.DashboardLogic = DashboardLogic;
  }
})(typeof window !== 'undefined' ? window : globalThis);
