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
  var levelCaptionEl = document.getElementById('chart-level-caption');

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

  /* Maps each known level string to the CSS custom property that already
   * colors its badge (style.css:16-22), so the level chart and the level
   * badge can never visually drift apart. An unrecognized string falls back
   * to the same "--level-unknown" var deriveLevelBadge already falls back
   * to for its CSS class, mirroring that known/fallback shape exactly. */
  var LEVEL_COLOR_VARS = {
    FULL: '--level-full',
    MID: '--level-mid',
    LOW: '--level-low',
    FAULT: '--level-fault',
    UNKNOWN: '--level-unknown',
  };

  function levelColor(levelStr) {
    var varName = LEVEL_COLOR_VARS[levelStr] || '--level-unknown';
    var value = getComputedStyle(document.documentElement).getPropertyValue(varName);
    return value ? value.trim() : '#6b6f6d';
  }

  /* Diagonal-hatch fill for a rect, clipped to that rect so the strokes
   * never bleed into a neighboring band. FAULT uses a denser, more opaque
   * hatch than UNKNOWN so the two states are never visually confusable with
   * each other (AC-HAPPY-3), on top of already having different labels and
   * fill colors. */
  function drawHatch(ctx, x0, y0, x1, y1, spacing, alpha) {
    var w = x1 - x0;
    var h = y1 - y0;
    if (w <= 0 || h <= 0) {
      return;
    }
    ctx.save();
    ctx.beginPath();
    ctx.rect(x0, y0, w, h);
    ctx.clip();
    ctx.strokeStyle = 'rgba(0, 0, 0, ' + alpha + ')';
    ctx.lineWidth = 1;
    var diag = w + h;
    for (var off = -h; off < diag; off += spacing) {
      ctx.beginPath();
      ctx.moveTo(x0 + off, y1);
      ctx.lineTo(x0 + off + h, y0);
      ctx.stroke();
    }
    ctx.restore();
  }

  /**
   * Draws the water-level chart: a single-row band/step strip, one segment
   * per contiguous run of identical `level` readings from
   * DashboardLogic.buildLevelBands, sharing the same time (X) axis as the
   * two numeric charts. FAULT and UNKNOWN segments additionally carry a
   * diagonal-hatch fill (denser for FAULT) so they are never distinguished
   * by color alone. `series` is the dashboard-wide series (for the
   * aria-label + time axis).
   */
  function drawLevelChart(canvas, series, opts) {
    if (!canvas || !canvas.getContext) {
      return;
    }
    var ctx = canvas.getContext('2d');
    var w = canvas.width;
    var h = canvas.height;
    ctx.clearRect(0, 0, w, h);

    canvas.setAttribute('aria-label', window.DashboardLogic.buildLevelChartAriaLabel(series));

    var result = window.DashboardLogic.buildLevelBands(series);

    if (result.state !== 'ok') {
      drawEmptyState(ctx, w, h, result.emptyMessage);
      if (opts.captionEl) {
        opts.captionEl.textContent = '';
      }
      return;
    }

    var timeAxis = window.DashboardLogic.buildTimeAxis(series);

    var plotLeft = PLOT_MARGIN_LEFT;
    var plotRight = w - PLOT_MARGIN_RIGHT;
    var plotTop = PLOT_MARGIN_TOP;
    var plotBottom = h - PLOT_MARGIN_BOTTOM;
    var plotWidth = plotRight - plotLeft;
    var plotHeight = plotBottom - plotTop;

    // Band strip occupies ~60% of the plot area's height, vertically
    // centered, leaving breathing room above/below for the FAULT hatch and
    // the time-tick row.
    var stripHeight = plotHeight * 0.6;
    var stripTop = plotTop + (plotHeight - stripHeight) / 2;
    var stripBottom = stripTop + stripHeight;

    var bands = result.bands;

    function toX(fx) {
      return plotLeft + fx * plotWidth;
    }

    for (var i = 0; i < bands.length; i++) {
      var band = bands[i];
      var left = i === 0 ? 0 : (bands[i - 1].x1 + band.x0) / 2;
      var right = i === bands.length - 1 ? 1 : (band.x1 + bands[i + 1].x0) / 2;
      var x0 = toX(left);
      var x1 = toX(right);

      ctx.fillStyle = levelColor(band.level);
      ctx.fillRect(x0, stripTop, x1 - x0, stripHeight);

      if (band.level === 'FAULT') {
        drawHatch(ctx, x0, stripTop, x1, stripBottom, 6, 0.35);
      } else if (band.level === 'UNKNOWN') {
        drawHatch(ctx, x0, stripTop, x1, stripBottom, 12, 0.15);
      }

      ctx.font = TICK_FONT;
      var labelWidth = ctx.measureText(band.level).width;
      if (labelWidth + 8 <= x1 - x0) {
        ctx.fillStyle = '#ffffff';
        ctx.textAlign = 'center';
        ctx.textBaseline = 'middle';
        ctx.fillText(band.level, (x0 + x1) / 2, stripTop + stripHeight / 2);
        ctx.textAlign = 'start';
        ctx.textBaseline = 'alphabetic';
      }
    }

    // Strip frame, matching the numeric charts' plot-area border idiom.
    ctx.strokeStyle = '#d1d5db';
    ctx.lineWidth = 1;
    ctx.strokeRect(plotLeft, stripTop, plotWidth, stripHeight);

    // X tick labels / clock-not-synced placeholder — shared time base, same
    // idiom as drawMetricChart.
    ctx.font = TICK_FONT;
    ctx.fillStyle = '#6b7280';
    ctx.textBaseline = 'top';
    if (timeAxis.state === 'ok') {
      for (var x = 0; x < timeAxis.ticks.length; x++) {
        var tick = timeAxis.ticks[x];
        var txPos = toX(tick.x);
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
        drawLevelChart(document.getElementById('chart-level'), series, {
          captionEl: levelCaptionEl,
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
