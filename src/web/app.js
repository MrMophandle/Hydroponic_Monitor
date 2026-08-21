/**
 * app.js — thin, DOM-only half of the dashboard (Pure-Logic / Device-Only
 * Split, browser side). Owns fetch(), setInterval(), and all element
 * updates; every interpretation decision (badge text/class, timestamp
 * formatting, gap handling, pre-first-sample detection) is delegated to
 * window.DashboardLogic (dashboard-logic.js), which is loaded first and is
 * unit-tested under Node (test/web/dashboard-logic.test.mjs). This file has
 * no host test — it is bench-verify-only per the Phase 6 Test Strategy
 * exception, same as http_api.c's routing.
 *
 * Poll interval matches the sampler's ~30s sample interval (Success
 * Criteria: "observable within ~30s ... without the user reloading the
 * page"). A failed poll never wipes the last-known values — it only
 * updates the poll-status line, so a transient network blip on the browser
 * side never looks like an AC-ERROR-1 sensor failure.
 */
(function () {
  'use strict';

  var POLL_INTERVAL_MS = 30000;
  var WAITING_TEXT = 'waiting for first reading';

  var tempValueEl = document.getElementById('temp-value');
  var tempBadgeEl = document.getElementById('temp-badge');
  var lightValueEl = document.getElementById('light-value');
  var lightBadgeEl = document.getElementById('light-badge');
  var levelValueEl = document.getElementById('level-value');
  var levelBadgeEl = document.getElementById('level-badge');
  var pollStatusEl = document.getElementById('poll-status');
  var chartCanvas = document.getElementById('history-chart');

  var haveFirstSample = false;
  var latestSeries = null;

  function setBadge(el, badge) {
    el.textContent = badge.text;
    el.className = 'badge ' + badge.cssClass;
  }

  function renderNow(nowPayload) {
    if (window.DashboardLogic.isPreFirstSample(nowPayload)) {
      tempValueEl.textContent = WAITING_TEXT;
      lightValueEl.textContent = WAITING_TEXT;
      levelValueEl.textContent = WAITING_TEXT;
      tempBadgeEl.textContent = '';
      lightBadgeEl.textContent = '';
      levelBadgeEl.textContent = '';
      tempBadgeEl.className = 'badge';
      lightBadgeEl.className = 'badge';
      levelBadgeEl.className = 'badge';
      return;
    }

    haveFirstSample = true;

    var tempValid = !!(nowPayload.valid && nowPayload.valid.temp);
    var lightValid = !!(nowPayload.valid && nowPayload.valid.light);

    tempValueEl.textContent = tempValid ? nowPayload.temp_c.toFixed(2) + ' °C' : '—';
    setBadge(tempBadgeEl, window.DashboardLogic.deriveMetricBadge(tempValid));

    lightValueEl.textContent = lightValid ? nowPayload.lux.toFixed(2) + ' lux' : '—';
    setBadge(lightBadgeEl, window.DashboardLogic.deriveMetricBadge(lightValid));

    levelValueEl.textContent = nowPayload.level;
    setBadge(levelBadgeEl, window.DashboardLogic.deriveLevelBadge(nowPayload.level));
  }

  function setPollStatus(text, isStale) {
    pollStatusEl.textContent = text;
    pollStatusEl.className = 'poll-status' + (isStale ? ' stale' : '');
  }

  /* A baseline and left edge so the plot area always reads as a chart, even
   * when it holds no series. Without this the empty state is a bare message
   * floating in white space. */
  function drawFrame(ctx, w, h) {
    ctx.strokeStyle = '#d1d5db';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(10, 5);
    ctx.lineTo(10, h - 10);
    ctx.lineTo(w - 10, h - 10);
    ctx.stroke();
  }

  function drawChart(series) {
    if (!chartCanvas || !chartCanvas.getContext) {
      return;
    }
    var ctx = chartCanvas.getContext('2d');
    var w = chartCanvas.width;
    var h = chartCanvas.height;
    ctx.clearRect(0, 0, w, h);

    /* A canvas cannot expose its pixels to assistive tech, so the chart's
     * only screen-reader representation is this label. Refreshed on every
     * draw so it never describes stale data. */
    chartCanvas.setAttribute('aria-label', window.DashboardLogic.buildChartAriaLabel(series));

    var n = series.labels.length;

    /* Empty state. Previously this function cleared the canvas and returned,
     * leaving a blank 900x320 box that looked identical to a failed render —
     * which is exactly what the bench saw with both sensors unplugged
     * (2026-08-20). Say so explicitly instead. */
    if (n === 0 || !window.DashboardLogic.hasPlottableData(series)) {
      drawFrame(ctx, w, h);
      ctx.fillStyle = '#6b7280';
      ctx.font = '14px system-ui, -apple-system, sans-serif';
      ctx.textAlign = 'center';
      ctx.textBaseline = 'middle';
      ctx.fillText(
        n === 0 ? 'no readings recorded yet' : 'no data to plot — sensors offline',
        w / 2,
        h / 2
      );
      ctx.textAlign = 'start';
      ctx.textBaseline = 'alphabetic';
      return;
    }

    drawFrame(ctx, w, h);

    function plotSeries(values, color) {
      var finite = values.filter(function (v) {
        return typeof v === 'number' && isFinite(v);
      });
      if (finite.length === 0) {
        return;
      }
      var min = Math.min.apply(null, finite);
      var max = Math.max.apply(null, finite);
      var range = max - min || 1;

      ctx.strokeStyle = color;
      ctx.lineWidth = 2;
      ctx.beginPath();

      var drawing = false;
      for (var i = 0; i < n; i++) {
        var v = values[i];
        var x = (i / Math.max(n - 1, 1)) * (w - 20) + 10;
        if (v === null || v === undefined || !isFinite(v)) {
          // Gap: lift the pen so we never interpolate across a null.
          drawing = false;
          continue;
        }
        var y = h - 10 - ((v - min) / range) * (h - 20);
        if (!drawing) {
          ctx.moveTo(x, y);
          drawing = true;
        } else {
          ctx.lineTo(x, y);
        }
      }
      ctx.stroke();
    }

    plotSeries(series.temp_c, '#b8420b');
    plotSeries(series.lux, '#1c7c3c');
  }

  function fetchHistory() {
    return fetch('/api/history?points=180')
      .then(function (resp) {
        if (!resp.ok) {
          throw new Error('history fetch failed: ' + resp.status);
        }
        return resp.json();
      })
      .then(function (historyPayload) {
        latestSeries = window.DashboardLogic.buildChartSeries(historyPayload);
        drawChart(latestSeries);
      })
      .catch(function (err) {
        // Leave the last-known chart visible; only the status line reflects
        // the failure (no fabricated wiped/zeroed state — the browser-side
        // equivalent of AC-ERROR-1).
        setPollStatus('history update failed: ' + err.message, true);
      });
  }

  function fetchNow() {
    return fetch('/api/now')
      .then(function (resp) {
        if (!resp.ok) {
          throw new Error('now fetch failed: ' + resp.status);
        }
        return resp.json();
      })
      .then(function (nowPayload) {
        renderNow(nowPayload);
        var ts = window.DashboardLogic.formatReadingTimestamp(nowPayload.t, nowPayload.time_valid);
        setPollStatus(ts ? 'last updated ' + ts : 'last updated (clock not synced)', false);
      })
      .catch(function (err) {
        // Never wipe/zero the last-known values on a transient poll
        // failure — only surface that the poll itself failed.
        setPollStatus('update failed: ' + err.message, true);
      });
  }

  function poll() {
    fetchNow();
    fetchHistory();
  }

  fetchHistory();
  fetchNow();
  setInterval(poll, POLL_INTERVAL_MS);
})();
