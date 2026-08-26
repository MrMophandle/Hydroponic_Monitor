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
  var tempCaptionEl = document.getElementById('chart-temp-caption');
  var lightCaptionEl = document.getElementById('chart-light-caption');

  var haveFirstSample = false;

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
   * floating in white space. Mirrors the frame + centered-text idiom the
   * single-chart renderer used before the Phase 3 split into per-metric
   * panels. */
  function drawEmptyState(ctx, w, h, message) {
    ctx.strokeStyle = '#d1d5db';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(10, 5);
    ctx.lineTo(10, h - 10);
    ctx.lineTo(w - 10, h - 10);
    ctx.stroke();

    ctx.fillStyle = '#6b7280';
    ctx.font = '14px system-ui, -apple-system, sans-serif';
    ctx.textAlign = 'center';
    ctx.textBaseline = 'middle';
    ctx.fillText(message, w / 2, h / 2);
    ctx.textAlign = 'start';
    ctx.textBaseline = 'alphabetic';
  }

  /* Plot-area margins, reserving room for Y tick labels (left) and X tick
   * labels (bottom). Chosen to comfortably fit the widest tick strings this
   * dashboard produces ("-12.3" for temp, four-digit lux values) at the
   * 14px tick font below. */
  var PLOT_MARGIN_LEFT = 45;
  var PLOT_MARGIN_RIGHT = 10;
  var PLOT_MARGIN_TOP = 10;
  var PLOT_MARGIN_BOTTOM = 25;
  var TICK_FONT = '14px system-ui, -apple-system, sans-serif';

  /**
   * Draws one numeric metric's (water temperature or ambient light) chart:
   * frame, Y gridlines + tick labels, the line trace (walking the axis's
   * already gap-split segments — never re-deriving gap handling here), and
   * X tick labels / caption from the shared time axis. `series` is the
   * dashboard-wide series (for the aria-label + time axis); `values` is this
   * metric's own array within it.
   */
  function drawMetricChart(canvas, series, values, opts) {
    if (!canvas || !canvas.getContext) {
      return;
    }
    var ctx = canvas.getContext('2d');
    var w = canvas.width;
    var h = canvas.height;
    ctx.clearRect(0, 0, w, h);

    var ariaLabel =
      opts.title === 'Water Temperature'
        ? window.DashboardLogic.buildTempChartAriaLabel(series)
        : window.DashboardLogic.buildLightChartAriaLabel(series);
    canvas.setAttribute('aria-label', ariaLabel);

    var axis = window.DashboardLogic.buildMetricAxis(values, { unit: opts.unit, title: opts.title });
    var timeAxis = window.DashboardLogic.buildTimeAxis(series);

    if (axis.state !== 'ok') {
      drawEmptyState(ctx, w, h, axis.emptyMessage);
      if (opts.captionEl) {
        opts.captionEl.textContent = '';
      }
      return;
    }

    var plotLeft = PLOT_MARGIN_LEFT;
    var plotRight = w - PLOT_MARGIN_RIGHT;
    var plotTop = PLOT_MARGIN_TOP;
    var plotBottom = h - PLOT_MARGIN_BOTTOM;
    var plotWidth = plotRight - plotLeft;
    var plotHeight = plotBottom - plotTop;

    function toPx(fx, fy) {
      return { x: plotLeft + fx * plotWidth, y: plotBottom - fy * plotHeight };
    }

    // Frame: left edge + baseline of the plot area.
    ctx.strokeStyle = '#d1d5db';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(plotLeft, plotTop);
    ctx.lineTo(plotLeft, plotBottom);
    ctx.lineTo(plotRight, plotBottom);
    ctx.stroke();

    // Y gridlines + tick labels.
    ctx.font = TICK_FONT;
    ctx.fillStyle = '#6b7280';
    ctx.textAlign = 'right';
    ctx.textBaseline = 'middle';
    for (var i = 0; i < axis.tickPositions.length; i++) {
      var ty = toPx(0, axis.tickPositions[i]).y;
      ctx.strokeStyle = '#e5e7eb';
      ctx.lineWidth = 1;
      ctx.beginPath();
      ctx.moveTo(plotLeft, ty);
      ctx.lineTo(plotRight, ty);
      ctx.stroke();
      ctx.fillText(axis.tickLabels[i], plotLeft - 6, ty);
    }
    ctx.textAlign = 'start';
    ctx.textBaseline = 'alphabetic';

    // Line trace: iterate the already gap-split segments — moveTo at the
    // start of each run, lineTo for the rest. Never merge across segments
    // (the "lift the pen, never interpolate across a null" contract).
    ctx.strokeStyle = opts.color;
    ctx.lineWidth = 2;
    for (var s = 0; s < axis.segments.length; s++) {
      var segment = axis.segments[s];
      ctx.beginPath();
      for (var p = 0; p < segment.length; p++) {
        var px = toPx(segment[p].x, segment[p].y);
        if (p === 0) {
          ctx.moveTo(px.x, px.y);
        } else {
          ctx.lineTo(px.x, px.y);
        }
      }
      ctx.stroke();
    }

    // X tick labels / clock-not-synced placeholder.
    ctx.font = TICK_FONT;
    ctx.fillStyle = '#6b7280';
    ctx.textBaseline = 'top';
    if (timeAxis.state === 'ok') {
      for (var x = 0; x < timeAxis.ticks.length; x++) {
        var tick = timeAxis.ticks[x];
        var txPos = toPx(tick.x, 0).x;
        ctx.textAlign = x === 0 ? 'left' : x === timeAxis.ticks.length - 1 ? 'right' : 'center';
        ctx.fillText(tick.label, txPos, plotBottom + 6);
      }
    } else if (timeAxis.state === 'clock-not-synced') {
      ctx.textAlign = 'center';
      ctx.fillText('clock not synced', (plotLeft + plotRight) / 2, plotBottom + 6);
    }
    ctx.textAlign = 'start';
    ctx.textBaseline = 'alphabetic';

    if (opts.captionEl) {
      opts.captionEl.textContent = timeAxis.state === 'ok' ? timeAxis.caption : '';
    }
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
        var series = window.DashboardLogic.buildChartSeries(historyPayload);
        drawMetricChart(document.getElementById('chart-temp'), series, series.temp_c, {
          unit: '°C',
          title: 'Water Temperature',
          color: '#b8420b',
          captionEl: tempCaptionEl,
        });
        drawMetricChart(document.getElementById('chart-light'), series, series.lux, {
          unit: 'lux',
          title: 'Ambient Light',
          color: '#1c7c3c',
          captionEl: lightCaptionEl,
        });
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
